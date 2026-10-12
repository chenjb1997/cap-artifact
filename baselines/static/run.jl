# Calendar adapter for two unchanged original static solvers.
# Author iterations, tolerances, data structures and printed output are retained.
const STATIC_LOAD_START = time_ns()
using SHA, Printf
include(joinpath(@__DIR__,"gzip_output.jl"))
const METHOD_INDEX = findfirst(==("--method"), ARGS)
METHOD_INDEX !== nothing && METHOD_INDEX < length(ARGS) || error("--method is required")
const STATIC_METHOD = ARGS[METHOD_INDEX+1]
STATIC_METHOD in ("www24_di","swamp_flow") || error("Invalid method")
const SOURCE_ROOT = STATIC_METHOD == "www24_di" ? joinpath(@__DIR__,"di","src") : joinpath(@__DIR__,"exactflow")
const AUTHOR_FILES = STATIC_METHOD == "www24_di" ? ["hlpp.jl","utils.jl","DHSG.jl"] : ["include/hlpp.jl","src/dense_flow.jl"]
module Native
using SparseArrays, Printf, Statistics, LinearAlgebra, Random
for filename in Main.AUTHOR_FILES
    include(joinpath(Main.SOURCE_ROOT,filename))
end
end
const STATIC_SOURCE_LOAD_NS = Int(time_ns()-STATIC_LOAD_START)
elapsed_ns(start::UInt64) = Int(time_ns() - start)

struct SwampEvent
    op::Char
    id::Int
    vertices::Vector{Int}
end
struct SwampUpdate
    step::Int
    op::Char
    update_ns::Int
    query_ns::Int
    edges::Int
    queried::Bool
    density::Union{Nothing,Float64}
    witness_size::Int
    author_kernel_seconds::Union{Nothing,Float64}
end
struct SwampQuery
    step::Int
    numerator::Int
    denominator::Int
    density::Float64
    vertices::Vector{Int}
    author_kernel_seconds::Float64
end

# Standard-library-only JSON. Nonfinite measurements are null, never fake zero.
function swamp_json(io::IO, value)
    if value === nothing
        print(io, "null")
    elseif value isa Bool
        print(io, value ? "true" : "false")
    elseif value isa Integer
        print(io, value)
    elseif value isa AbstractFloat
        print(io, isfinite(value) ? string(value) : "null")
    elseif value isa AbstractString || value isa Char
        print(io, '"')
        for c in string(value)
            if c == '"'; print(io, "\\\"")
            elseif c == '\\'; print(io, "\\\\")
            elseif c == '\n'; print(io, "\\n")
            elseif c == '\r'; print(io, "\\r")
            elseif c == '\t'; print(io, "\\t")
            elseif Int(c) < 0x20; @printf(io, "\\u%04x", Int(c))
            else; print(io, c)
            end
        end
        print(io, '"')
    elseif value isa AbstractVector || value isa Tuple
        print(io, '[')
        for (i, entry) in enumerate(value)
            i > 1 && print(io, ',')
            swamp_json(io, entry)
        end
        print(io, ']')
    elseif value isa AbstractDict
        print(io, '{')
        for (i, key) in enumerate(sort!(collect(keys(value)); by=string))
            i > 1 && print(io, ',')
            swamp_json(io, string(key)); print(io, ':'); swamp_json(io, value[key])
        end
        print(io, '}')
    else
        error("Unsupported JSON value type $(typeof(value))")
    end
end
function swamp_json_file(path::String, value)
    open(path, "w") do io
        swamp_json(io, value); println(io)
    end
end

function swamp_preload(path::String)
    events = SwampEvent[]
    active = Dict{Int,Vector{Int}}()
    n = 0; rank = 0
    open(path, "r") do io
        eof(io) && error("Empty input")
        header = split(readline(io))
        length(header) == 1 || error("Expected single n header")
        n = parse(Int, header[1]); n > 0 || error("n must be positive")
        for (line_index, line) in enumerate(eachline(io))
            isempty(strip(line)) && continue
            tokens = split(line)
            length(tokens) >= 4 || error("Malformed event at line $(line_index + 1)")
            tokens[1] in ("+", "-") || error("Unknown update operation")
            op = only(tokens[1]); id = parse(Int, tokens[2]); k = parse(Int, tokens[3])
            id >= 0 && k >= 1 && length(tokens) == k + 3 || error("Invalid edge ID/rank/length")
            vertices = sort!(parse.(Int, tokens[4:end]))
            all(v -> 0 <= v < n, vertices) || error("Endpoint outside [0,n)")
            length(unique(vertices)) == k || error("Repeated endpoint in a hyperedge")
            if op == '+'
                haskey(active, id) && error("Reused active edge ID")
                active[id] = vertices
            else
                haskey(active, id) && active[id] == vertices || error("Illegal deletion")
                delete!(active, id)
            end
            rank = max(rank, k)
            push!(events, SwampEvent(op, id, vertices))
        end
    end
    isempty(events) && error("At least one event is required")
    return n, rank, events
end


function static_query(active::Dict{Int,Vector{Int}}, step::Int, n::Int)
    ids=sort!(collect(keys(active)))
    edges=Vector{Vector{Int}}([copy(active[id]) for id in ids])
    isempty(edges) && return SwampQuery(step,0,0,0.0,Int[],0.0)
    if STATIC_METHOD == "swamp_flow"
        rewards=[vcat(zeros(Float64,length(e)),1.0) for e in edges]
        author_set,seconds=Native.densest(edges,rewards)
        value=Native.density(edges,author_set,rewards)
        members=sort!(collect(author_set))
    else
        # The original WWW24 API is one-based and consumes both incidence views.
        one_based=[edge .+ 1 for edge in edges]
        H=Native.elist2inc(one_based,n)
        Ht=Native.sparse(H')
        answer=Native.DHSG_flow_density_improvement(H,Ht;flowtol=1e-8)
        members=sort!(Int.(answer["optsol"]) .- 1)
        value=Float64(answer["optval"])
        seconds=Float64(answer["total_dt"])
    end
    # Output is independently recounted outside performance clocks.
    numerator=round(Int,value*length(members))
    return SwampQuery(step,numerator,length(members),value,members,Float64(seconds))
end
function swamp_verify(query::SwampQuery, active::Dict{Int,Vector{Int}}, n::Int)
    members = Set(query.vertices)
    length(members) == query.denominator || error("Repeated returned vertex")
    all(v -> 0 <= v < n, members) || error("Returned vertex outside input universe")
    count_inside = count(edge -> all(v -> v in members, edge), values(active))
    independent_density = isempty(members) ? 0.0 : count_inside / length(members)
    count_inside == query.numerator || error("Returned numerator differs from induced edge count")
    isapprox(independent_density, query.density; atol=1e-10, rtol=1e-10) || error("Returned density mismatch")
end

struct SwampCalendarReport
    step::Int
    label::String
end
struct SwampCalendarAnswer
    report::SwampCalendarReport
    answer::SwampQuery
    query_ns::Int
end
struct SwampCalendarUpdate
    step::Int
    op::Char
    update_ns::Int
    query_ns::Int
    edges::Int
    query_count::Int
    density::Union{Nothing,Float64}
    witness_size::Int
    author_kernel_seconds::Union{Nothing,Float64}
end

function swamp_calendar_read(path::String, update_count::Int)
    reports = SwampCalendarReport[]
    first_record = true
    open(path, "r") do io
        for line in eachline(io)
            isempty(line) && continue
            if first_record && line == "step\tlabel"
                first_record = false
                continue
            end
            first_record = false
            fields = split(line, '\t'; limit=2, keepempty=true)
            length(fields) == 2 || error("Expected zero-based step<TAB>label")
            token = fields[1]
            !isempty(token) && all(c -> '0' <= c <= '9', token) || error("Invalid query step")
            step = tryparse(Int, token)
            step !== nothing && 0 <= step < update_count || error("Query step out of range")
            isempty(reports) || step >= reports[end].step || error("Query steps decrease")
            push!(reports, SwampCalendarReport(step, String(fields[2])))
        end
    end
    return reports
end

function swamp_calendar_write(update_io::IO, witness_io::IO,
                              records::Vector{SwampCalendarUpdate}, answers::Vector{SwampCalendarAnswer},
                              written_updates::Int, written_queries::Int, delta::WitnessDeltaEncoder)
    for j in eachindex(records)
        r = records[j]
        print(update_io, r.step, ',', r.op, ',', r.update_ns, ',', r.update_ns, ',',
              r.query_ns, ',', r.update_ns + r.query_ns, ',', r.edges, ',', Int(r.query_count > 0), ',')
        r.density !== nothing && print(update_io, r.density)
        print(update_io, ',')
        r.query_count > 0 && print(update_io, r.witness_size)
        print(update_io, ',')
        r.author_kernel_seconds !== nothing && print(update_io, r.author_kernel_seconds)
        println(update_io, ',', r.query_count)
    end
    for j in eachindex(answers)
        item = answers[j]; q = item.answer
        swamp_json(witness_io, merge(witness_delta!(delta,q.vertices), Dict{String,Any}("step"=>q.step, "label"=>item.report.label,
            "numerator"=>q.numerator, "denominator"=>q.denominator, "density"=>q.density,
            "upper"=>nothing, "author_kernel_seconds"=>q.author_kernel_seconds,
            "query_ns"=>item.query_ns)))
        println(witness_io)
    end
    written_updates += length(records); written_queries += length(answers)
    empty!(records); empty!(answers)
    return written_updates, written_queries
end

function swamp_calendar_batch(events::Vector{SwampEvent}, n::Int, prefix::String,
                              reports::Vector{SwampCalendarReport}, checkpoint::Int,
                              budget::Float64, check::Bool, stop_after::Int=0)
    init_start = time_ns()
    active = Dict{Int,Vector{Int}}()
    delta = WitnessDeltaEncoder(nothing)
    records = SwampCalendarUpdate[]; sizehint!(records, 1)
    answers = SwampCalendarAnswer[]; sizehint!(answers, 1)
    update_io = GzipOutput(prefix * ".csv.gz")
    witness_io = GzipOutput(prefix * ".witnesses.jsonl.gz")
    println(update_io, "step,op,update_ns,bookkeeping_ns,query_ns,total_service_ns,edges,queried,density,witness_size,author_kernel_seconds,query_count")
    flush(update_io); flush(witness_io)
    initialization_ns = elapsed_ns(init_start)
    total_update = 0; total_query = 0; validation_ns = 0; checkpoint_ns = 0
    total_author_seconds = 0.0; written_updates = 0; written_queries = 0; next_report = 1
    status = "complete"
    batch_start = time_ns()
    try
        for (index, event) in enumerate(events)
            stop_after > 0 && index > stop_after && break
            step = index - 1
            start = time_ns()
            if event.op == '+'
                active[event.id] = event.vertices
            else
                delete!(active, event.id)
            end
            update_ns = elapsed_ns(start); total_update += update_ns
            query_ns = 0; query_count = 0; observed_density = nothing; witness_size = -1
            author_seconds = nothing
            while next_report <= length(reports) && reports[next_report].step == step
                start = time_ns()
                # Unchanged frozen query: edge/reward construction, original
                # densest and density calls, and answer-set materialization.
                answer = static_query(active, step, n)
                one_query_ns = elapsed_ns(start)
                query_ns += one_query_ns; total_query += one_query_ns; query_count += 1
                observed_density = answer.density; witness_size = answer.denominator
                author_seconds = (author_seconds === nothing ? 0.0 : author_seconds) + answer.author_kernel_seconds
                total_author_seconds += answer.author_kernel_seconds
                push!(answers, SwampCalendarAnswer(reports[next_report], answer, one_query_ns))
                if check
                    # Independent recount remains outside native solve/service
                    # clocks, exactly as in the frozen uniform stream driver.
                    start = time_ns(); swamp_verify(answer, active, n); validation_ns += elapsed_ns(start)
                end
                written_updates, written_queries = swamp_calendar_write(update_io, witness_io, records, answers, written_updates, written_queries, delta)
                flush(witness_io)
                next_report += 1
            end
            push!(records, SwampCalendarUpdate(step, event.op, update_ns, query_ns, length(active),
                query_count, observed_density, witness_size, author_seconds))
            written_updates, written_queries = swamp_calendar_write(update_io, witness_io, records, answers, written_updates, written_queries, delta)
            if checkpoint > 0 && index % checkpoint == 0
                start = time_ns()
                written_updates, written_queries = swamp_calendar_write(update_io, witness_io,
                    records, answers, written_updates, written_queries, delta)
                flush(update_io); flush(witness_io)
                swamp_json_file(prefix * ".checkpoint.$index.json", Dict{String,Any}(
                    "status"=>"checkpoint", "completed_updates"=>index, "last_step"=>step,
                    "planned_updates"=>length(events), "queries"=>written_queries,
                    "update_ns"=>total_update, "query_ns"=>total_query,
                    "csv_completed_update_rows"=>written_updates, "witness_completed_query_rows"=>written_queries,
                    "batch_elapsed_ns_includes_checkpoint_io"=>elapsed_ns(batch_start),
                    "previous_checkpoint_ns"=>checkpoint_ns))
                checkpoint_ns += elapsed_ns(start)
            end
            if budget > 0 && index < length(events) && elapsed_ns(batch_start) >= budget * 1e9
                status = "budget_exceeded"; break
            end
        end
        batch_ns = elapsed_ns(batch_start)
        start = time_ns()
        swamp_calendar_write(update_io, witness_io, records, answers, written_updates, written_queries, delta)
        flush(update_io); flush(witness_io)
        final_serialization_ns = elapsed_ns(start)
        return Dict{String,Any}(
            "status"=>status, "updates"=>written_updates, "planned_updates"=>length(events),
            "queries"=>written_queries, "planned_queries"=>length(reports), "active_edges"=>length(active),
            "update_ns"=>total_update, "total_update_ns"=>total_update,
            "service_update_ns"=>total_update, "query_ns"=>total_query, "total_query_ns"=>total_query,
            "total_service_ns"=>total_update + total_query, "author_kernel_seconds"=>total_author_seconds,
            "initialization_ns"=>initialization_ns, "batch_ns"=>batch_ns,
            "validation_ns_excluded_from_service"=>validation_ns, "checkpoint_ns"=>checkpoint_ns,
            "batch_includes_checkpoint_io"=>true, "batch_includes_validation"=>check,
            "final_sidecar_serialization_ns"=>final_serialization_ns,
            "checkpoint_cadence"=>checkpoint, "budget_seconds"=>budget, "query_cadence"=>nothing,
            "validation_enabled"=>check, "input_preloaded"=>true, "enumeration_timed"=>true,
            "stable_edge_order"=>"ascending logical edge ID", "author_prints_preserved"=>true,
            "empty_query_handling"=>"unchanged frozen wrapper zero/empty response before original count_nodes",
            "auto_tail_query"=>false, "query_step_definition"=>"zero-based update index; after that update",
            "update_scope"=>"Dict insertion/deletion only; original static solver recomputed in every query",
            "query_scope"=>"sort active IDs, construct native input/rewards or incidence matrices, unchanged native solver, materialize/sort members; original prints included",
            "upper_bound_scope"=>"none exported; numerical exact method is not a verified dual certificate",
            "prefix_replay"=>stop_after>0, "sidecar_storage"=>"streaming gzip level1",
            "buffered_updates"=>length(records),
            "buffered_witness_vertices"=>sum(item -> length(item.answer.vertices), answers; init=0))
    finally
        close(update_io); close(witness_io)
    end
end


function static_main(args::Vector{String})
    length(args)>=4 && iseven(length(args)) || error("INPUT PREFIX --method NAME --queries FILE required")
    input=abspath(args[1]);prefix=abspath(args[2])
    query_path=nothing;checkpoint=1000;budget=270.0;check=false;stop_after=0
    for i in 3:2:length(args)
        key=args[i];value=args[i+1]
        if key=="--method";value==STATIC_METHOD || error("method mismatch")
        elseif key=="--queries";query_path=abspath(value)
        elseif key=="--checkpoint";checkpoint=parse(Int,value)
        elseif key=="--budget-seconds";budget=parse(Float64,value)
        elseif key=="--check";value in ("0","1") || error("bad check");check=value=="1"
        elseif key=="--stop-after-updates";stop_after=parse(Int,value);stop_after>0 || error("Empty prefix")
        else;error("Unknown option $key")
        end
    end
    query_path!==nothing || error("Missing queries")
    stop_after==0 || check || error("Prefix replay requires --check 1")
    checkpoint>=0 && isfinite(budget) && budget>=0 || error("Invalid limits")
    mkpath(dirname(prefix))
    any(x->startswith(x,basename(prefix)*"."),readdir(dirname(prefix))) && error("Use fresh prefix")
    start=time_ns();n,rank,events=swamp_preload(input)
    if STATIC_METHOD=="www24_di"
        all(e->length(e.vertices)>=2,events) || error("Original WWW24 requires rank >= 2")
    end
    reports=swamp_calendar_read(query_path,length(events));input_ns=elapsed_ns(start)
    source_hashes=Dict(name=>bytes2hex(sha256(read(joinpath(SOURCE_ROOT,name)))) for name in AUTHOR_FILES)
    warm_dir=prefix*".jit";mkdir(warm_dir)
    warm_events=[SwampEvent('+',0,[0,1]),SwampEvent('+',1,[0,1]),
        SwampEvent('+',2,[0,1,2]),SwampEvent('+',3,[1,2,3]),
        SwampEvent('-',1,[0,1]),SwampEvent('-',0,[0,1]),
        SwampEvent('-',2,[0,1,2]),SwampEvent('-',3,[1,2,3])]
    warm_reports=[SwampCalendarReport(i-1,string(i-1)) for i in eachindex(warm_events)]
    start=time_ns()
    warm=swamp_calendar_batch(warm_events,4,joinpath(warm_dir,"warmup"),warm_reports,1,0.0,true)
    swamp_json_file(joinpath(warm_dir,"warmup.stats.json"),warm)
    jit_ns=elapsed_ns(start)
    stats=swamp_calendar_batch(events,n,prefix,reports,checkpoint,budget,check,stop_after)
    merge!(stats,Dict{String,Any}("method"=>STATIC_METHOD,"n"=>n,"rank"=>rank,
        "input_ns"=>input_ns,"jit_warmup_ns"=>jit_ns,"jit_warmup_updates"=>8,
        "jit_warmup_scope"=>"independent tiny graph through same wrapper and native solver; compilation, execution and IO",
        "source_load_ns"=>STATIC_SOURCE_LOAD_NS,"julia_version"=>string(VERSION),
        "julia_threads"=>Threads.nthreads(),"source_root"=>SOURCE_ROOT,"source_hashes"=>source_hashes,
        "driver_sha256"=>bytes2hex(sha256(read(@__FILE__))),
        "native_flow_tolerance"=>STATIC_METHOD=="www24_di" ? 1e-8 : 1e-6,
        "input_sha256"=>bytes2hex(sha256(read(input))),"queries_path"=>query_path,
        "queries_sha256"=>bytes2hex(sha256(read(query_path))),
        "quality_verification"=>"independent exact integer oracle required after timing; no certificate invented",
        "no_solver_warm_start"=>true,"author_algorithms_modified"=>false))
    swamp_json_file(prefix*".stats.json",stats)
    println("STATIC_CALENDAR_STATUS ",STATIC_METHOD," ",stats["status"]," updates=",stats["updates"])
    return stats["status"]=="complete" ? 0 : 124
end
try
    exit(static_main(ARGS))
catch exception
    showerror(stderr,exception,catch_backtrace());println(stderr);exit(2)
end
