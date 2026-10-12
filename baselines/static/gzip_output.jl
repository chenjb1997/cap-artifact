# A bounded streaming gzip sink using Julia's bundled zlib. Sidecar IO only.
using Zlib_jll
mutable struct GzipOutput <: IO
    handle::Ptr{Cvoid}
    buffer::Vector{UInt8}
    count::Int
end
function GzipOutput(path::String)
    handle=ccall((:gzopen,Zlib_jll.libz),Ptr{Cvoid},(Cstring,Cstring),path,"wb1")
    handle==C_NULL && error("Cannot open gzip output")
    GzipOutput(handle,Vector{UInt8}(undef,65536),0)
end
Base.isopen(io::GzipOutput)=io.handle!=C_NULL
Base.iswritable(io::GzipOutput)=isopen(io)
function gzip_drain(io::GzipOutput)
    isopen(io) || error("Closed gzip output")
    if io.count>0
        count=ccall((:gzwrite,Zlib_jll.libz),Cint,(Ptr{Cvoid},Ptr{UInt8},Cuint),io.handle,io.buffer,io.count)
        count==io.count || error("gzip write failed")
        io.count=0
    end
    nothing
end
function Base.write(io::GzipOutput,value::UInt8)
    io.count==length(io.buffer) && gzip_drain(io)
    io.count+=1;io.buffer[io.count]=value;1
end
function Base.unsafe_write(io::GzipOutput,p::Ptr{UInt8},count::UInt)
    remaining=Int(count);offset=0
    while remaining>0
        io.count==length(io.buffer) && gzip_drain(io)
        chunk=min(remaining,length(io.buffer)-io.count)
        GC.@preserve io unsafe_copyto!(pointer(io.buffer,io.count+1),p+offset,chunk)
        io.count+=chunk;offset+=chunk;remaining-=chunk
    end
    count
end
function Base.flush(io::GzipOutput)
    gzip_drain(io)
    ccall((:gzflush,Zlib_jll.libz),Cint,(Ptr{Cvoid},Cint),io.handle,2)==0 || error("gzip flush failed")
    nothing
end
function Base.close(io::GzipOutput)
    if isopen(io)
        gzip_drain(io)
        status=ccall((:gzclose,Zlib_jll.libz),Cint,(Ptr{Cvoid},),io.handle)
        io.handle=C_NULL
        status==0 || error("gzip close failed")
    end
    nothing
end

# Set evidence only; all calls occur after native full-set materialization timer.
mutable struct WitnessDeltaEncoder
    previous::Union{Nothing,Vector{Int}}
end
function witness_delta!(delta::WitnessDeltaEncoder,vertices::Vector{Int})
    current=sort(copy(vertices))
    any(i->current[i]==current[i-1],2:length(current)) && error("Duplicate vertex in returned set")
    result=Dict{String,Any}("encoding"=>"set-delta-v1","vertex_count"=>length(current))
    if delta.previous===nothing
        result["vertices"]=current
    else
        result["added_vertices"]=setdiff(current,delta.previous)
        result["removed_vertices"]=setdiff(delta.previous,current)
    end
    delta.previous=current
    result
end
