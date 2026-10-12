// Independent Chekuri--Quanrud amortized-core reconstruction.
// Source: arXiv:2210.02611v1, Fig.2, Sections 3.4 and 6.
// Section 6 omits full deletion pseudocode. All changed endpoint directions
// are scheduled explicitly; see the reproduction protocol for the distinction
// from the paper's proved amortized/worst-case implementation.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace cq {
using Int = std::int64_t;
struct Edge;
struct Group;
struct Bucket;
struct Link {
    int group=-1,endpoint=-1;
    Bucket* bucket=nullptr;
    Link* prev=nullptr;
    Link* next=nullptr;
};
struct Bucket {
    Int label;
    Bucket* prev=nullptr;
    Bucket* next=nullptr;
    Link* first=nullptr;
};
// Section 3.3's two nested doubly linked lists. The cursor is the first
// bucket whose label is at least the current degree. All additions/relabels
// use the current degree; a unit degree change moves the cursor at most once.
class LabelIndex {
    Bucket* first=nullptr;
    Bucket* last=nullptr;
    Bucket* cursor=nullptr;
    Int degree=0;
public:
    LabelIndex()=default;
    LabelIndex(const LabelIndex&)=delete;
    LabelIndex& operator=(const LabelIndex&)=delete;
    ~LabelIndex(){while(first){Bucket* next=first->next;delete first;first=next;}}
    void shift(int delta){
        degree+=delta;
        if(delta==1){if(cursor&&cursor->label<degree)cursor=cursor->next;}
        else if(delta==-1){Bucket* before=cursor?cursor->prev:last;if(before&&before->label>=degree)cursor=before;}
        else throw std::runtime_error("nonunit degree change");
    }
    void add(Link& link,Int label){
        if(link.bucket||label!=degree)throw std::runtime_error("invalid bucket insertion");
        Bucket* b=cursor;
        if(!b||b->label!=label){
            b=new Bucket{label};b->next=cursor;b->prev=cursor?cursor->prev:last;
            if(b->prev)b->prev->next=b;else first=b;
            if(cursor)cursor->prev=b;else last=b;
            cursor=b;
        }
        link.bucket=b;link.prev=nullptr;link.next=b->first;
        if(b->first)b->first->prev=&link;
        b->first=&link;
    }
    void erase(Link& link){
        Bucket* b=link.bucket;if(!b)throw std::runtime_error("missing bucket entry");
        if(link.prev)link.prev->next=link.next;else b->first=link.next;
        if(link.next)link.next->prev=link.prev;
        link.bucket=nullptr;link.prev=link.next=nullptr;
        if(!b->first){
            if(cursor==b)cursor=b->next;
            if(b->prev)b->prev->next=b->next;else first=b->next;
            if(b->next)b->next->prev=b->prev;else last=b->prev;
            delete b;
        }
    }
    const Link* extreme(bool minimum)const{Bucket* b=minimum?first:last;return b?b->first:nullptr;}
    std::set<std::tuple<Int,int,int>> snapshot()const{
        std::set<std::tuple<Int,int,int>> result;Bucket* expected_cursor=nullptr;Bucket* previous=nullptr;
        for(Bucket* b=first;b;b=b->next){
            if(b->prev!=previous||!b->first||(previous&&previous->label>=b->label))throw std::runtime_error("bucket order");
            if(!expected_cursor&&b->label>=degree)expected_cursor=b;
            Link* prior=nullptr;
            for(Link* item=b->first;item;item=item->next){
                if(item->bucket!=b||item->prev!=prior)throw std::runtime_error("bucket linkage");
                if(!result.insert({b->label,item->group,item->endpoint}).second)throw std::runtime_error("duplicate bucket link");
                prior=item;
            }
            previous=b;
        }
        if(last!=previous||cursor!=expected_cursor)throw std::runtime_error("bucket cursor");
        return result;
    }
};
struct Group {
    int id = -1, head = -1;
    Int count = 0;
    Edge* edge = nullptr;
    std::vector<Int> labels;
    std::vector<Link> links;
};
struct Edge {
    int id;
    std::vector<int> vertices;
    std::vector<std::unique_ptr<Group>> groups;
};
using Key = std::tuple<Int,int,int>; // label, group id, endpoint index
struct Parameters {
    int n, rank, levels;
    Int copies;
    long double epsilon, alpha, growth, multiplier, beta;
    Parameters(int n_,int r_,long double eps):n(n_),rank(r_),epsilon(eps){
        if(n<1||rank<1||!(eps>0&&eps<=1))throw std::runtime_error("invalid parameters");
        growth=std::pow(1+eps,0.25L);
        levels=std::max(1,(int)std::ceil(std::log((long double)std::max(2,n))/std::log(growth)));
        alpha=std::expm1(std::log1p(eps)/(8*levels));
        multiplier=(1+alpha)*(1+alpha);
        beta=3+3*alpha; // conservative version of Lemma 3.6
        auto gap=1/std::sqrt(1+eps)-1/(1+eps);
        // The +1 avoids selecting below the real-valued sufficient threshold
        // on an exactly integral boundary due to floating-point rounding.
        copies=(Int)std::ceil(beta*levels*rank/(growth*gap))+1;
        if(copies<1||copies>1000000000LL)throw std::runtime_error("unsupported copy count");
    }
};
struct Answer {std::vector<int> vertices; Int induced=0, max_load=0; int level=0;};

class Core {
    std::vector<Group*> registry;
    std::vector<int> free_group_ids;
    std::vector<LabelIndex> incoming, outgoing;
    std::deque<std::pair<int,bool>> tasks;
    std::vector<unsigned char> queued_inc, queued_dec;
    // direction=true means a load increase; false means a decrease.
    void schedule(int v,bool direction){
        auto& flags=direction?queued_inc:queued_dec;
        if(!flags[v]){flags[v]=1;tasks.emplace_back(v,direction);}
    }
    void detach(Group& g){
        if(!g.count)return;
        for(int j=0;j<(int)g.edge->vertices.size();++j){
            int v=g.edge->vertices[j];
            auto& index=j==g.head?incoming[v]:outgoing[v];
            index.erase(g.links[j]);
        }
    }
    void attach(Group& g){
        if(!g.count)return;
        for(int j=0;j<(int)g.edge->vertices.size();++j){
            int v=g.edge->vertices[j];
            auto& index=j==g.head?incoming[v]:outgoing[v];
            index.add(g.links[j],g.labels[j]);
        }
    }
    void refresh(Group& g){
        detach(g);
        for(int j=0;j<(int)g.labels.size();++j)g.labels[j]=load[g.edge->vertices[j]];
        attach(g);
        ++relabels;
    }
    Group& get_group(Edge& e,int h){
        if(!e.groups[h]){
            auto g=std::make_unique<Group>();
            if(free_group_ids.empty()){g->id=(int)registry.size();registry.push_back(nullptr);}
            else{g->id=free_group_ids.back();free_group_ids.pop_back();}
            g->edge=&e;g->head=h;g->labels.resize(e.vertices.size());g->links.resize(e.vertices.size());
            for(int j=0;j<(int)e.vertices.size();++j){g->links[j].group=g->id;g->links[j].endpoint=j;}
            registry[g->id]=g.get();e.groups[h]=std::move(g);
        }
        return *e.groups[h];
    }
    int minimum(const Edge& e)const{
        int h=0;
        for(int j=1;j<(int)e.vertices.size();++j)
            if(load[e.vertices[j]]<load[e.vertices[h]])h=j;
        return h;
    }
    void remove_copy(Group& g){
        if(g.count<=0)throw std::runtime_error("remove absent copy");
        if(g.count==1)detach(g);
        --g.count;
    }
    void add_copy(Edge& e,int h){
        Group& g=get_group(e,h);
        if(g.count)detach(g);
        ++g.count;
        // Section 3.4 shares a single endpoint-label vector among copies
        // having the same head. Reset all such copies, as specified there.
        for(int j=0;j<(int)e.vertices.size();++j)g.labels[j]=load[e.vertices[j]];
        attach(g);
    }
    void flip(Group& g,int new_head){
        int old=g.edge->vertices[g.head], dest=g.edge->vertices[new_head];
        Edge& e=*g.edge;
        remove_copy(g);
        change_load(old,-1);change_load(dest,1);
        add_copy(e,new_head);
        schedule(old,false);schedule(dest,true);
        ++flips;
    }
    void process(int v,bool inc){
        while(true){
            auto& index=inc?incoming[v]:outgoing[v];
            const Link* link=index.extreme(inc);
            if(!link)return;
            Int label=link->bucket->label;
            bool violates=inc?load[v]>(1+p.alpha)*label+1:
                                  label>(1+p.alpha)*load[v]+1;
            if(!violates)return;
            Group& g=*registry.at(link->group);
            int low=minimum(*g.edge), head_vertex=g.edge->vertices[g.head];
            ++processed_groups;
            if(load[g.edge->vertices[low]]<load[head_vertex])flip(g,low);
            else refresh(g);
        }
    }
    void settle(){
        while(!tasks.empty()){
            auto [v,inc]=tasks.front();tasks.pop_front();
            (inc?queued_inc:queued_dec)[v]=0;
            process(v,inc);
        }
    }
    void change_load(int v,int delta){load[v]+=delta;incoming[v].shift(delta);outgoing[v].shift(delta);}
public:
    Parameters p;
    std::vector<Int> load;
    std::map<int,std::unique_ptr<Edge>> edges;
    Int physical_updates=0,flips=0,relabels=0,processed_groups=0;
    explicit Core(Parameters params):incoming(params.n),outgoing(params.n),
        queued_inc(params.n,0),queued_dec(params.n,0),p(params),load(params.n,0){}

    void insert(int id,const std::vector<int>& vertices,bool check_copies=false){
        if(edges.count(id)||vertices.empty()||vertices.size()>(size_t)p.rank)
            throw std::runtime_error("invalid insertion");
        auto edge=std::make_unique<Edge>();edge->id=id;edge->vertices=vertices;
        edge->groups.resize(vertices.size());Edge& e=*edge;edges.emplace(id,std::move(edge));
        for(Int i=0;i<p.copies;++i){
            int h=minimum(e),v=e.vertices[h];change_load(v,1);add_copy(e,h);
            schedule(v,true);settle();++physical_updates;
            if(check_copies)check(false);
        }
    }
    void erase(int id,const std::vector<int>& vertices,bool check_copies=false){
        auto it=edges.find(id);
        if(it==edges.end()||it->second->vertices!=vertices)throw std::runtime_error("invalid deletion");
        Edge& e=*it->second;
        for(Int i=0;i<p.copies;++i){
            Group* g=nullptr;
            for(auto& candidate:e.groups)if(candidate&&candidate->count){g=candidate.get();break;}
            if(!g)throw std::runtime_error("insufficient edge copies");
            int v=e.vertices[g->head];remove_copy(*g);change_load(v,-1);
            schedule(v,false);settle();++physical_updates;
            if(check_copies)check(false);
        }
        for(auto& g:e.groups)if(g){
            if(g->count)throw std::runtime_error("undeleted copy");
            registry[g->id]=nullptr;
            free_group_ids.push_back(g->id);
        }
        edges.erase(it);
    }
    void check(bool complete_edges=true)const{
        if(!tasks.empty())throw std::runtime_error("unsettled tasks");
        std::vector<Int> recounted(p.n,0);
        std::vector<std::set<Key>> in(p.n),out(p.n);
        for(const auto& [id,ep]:edges){
            const auto& e=*ep;Int total=0;
            for(const auto& gp:e.groups)if(gp&&gp->count){
                const auto& g=*gp;int h=g.head;total+=g.count;
                if(g.count<0)throw std::runtime_error("negative copies");
                recounted[e.vertices[h]]+=g.count;
                for(int j=0;j<(int)e.vertices.size();++j){
                    int u=e.vertices[j],v=e.vertices[h];
                    (j==h?in[u]:out[u]).insert({g.labels[j],g.id,j});
                    if(g.labels[h]>g.labels[j]+1)throw std::runtime_error("head-tail label order");
                    if(load[v]>(1+p.alpha)*g.labels[h]+1+1e-9L)
                        throw std::runtime_error("incoming label invariant");
                    if(j!=h&&g.labels[j]>(1+p.alpha)*load[u]+1+1e-9L)
                        throw std::runtime_error("outgoing label invariant");
                    if(load[v]>p.multiplier*load[u]+p.beta+1e-9L)
                        throw std::runtime_error("local orientation inequality");
                }
            }
            if(complete_edges&&total!=p.copies)throw std::runtime_error("copy conservation");
        }
        if(recounted!=load)throw std::runtime_error("load conservation");
        for(int v=0;v<p.n;++v)
            if(in[v]!=incoming[v].snapshot()||out[v]!=outgoing[v].snapshot())throw std::runtime_error("label-index mismatch");
    }
    Answer query()const{
        Answer result;
        if(edges.empty())return result;
        result.max_load=*std::max_element(load.begin(),load.end());
        long double threshold=result.max_load;
        auto count_above=[&](long double t){int count=0;for(Int d:load)count+=(d>=t);return count;};
        int previous=count_above(threshold);
        bool found=false;
        for(int i=0;i<p.levels;++i){
            long double next=(threshold-p.beta)/p.multiplier;
            int current=count_above(next);
            if(current<=p.growth*previous){
                for(int v=0;v<p.n;++v)if(load[v]>=next)result.vertices.push_back(v);
                result.level=i+1;found=true;break;
            }
            threshold=next;previous=current;
        }
        if(!found)throw std::runtime_error("no low-growth threshold");
        std::vector<unsigned char> in(p.n,0);
        for(int v:result.vertices)in[v]=1;
        for(const auto& [id,ep]:edges){
            bool contained=true;for(int v:ep->vertices)if(!in[v]){contained=false;break;}
            result.induced+=contained;
        }
        return result;
    }
};
} // namespace cq
