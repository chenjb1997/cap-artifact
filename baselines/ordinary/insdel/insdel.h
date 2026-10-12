#pragma once
#include <bits/stdc++.h>
namespace pvldb2024 {
using namespace std;
struct Edge
{
	int from, to;
	Edge(int t1, int t2) { from = t1, to = t2; }
	void re() { swap(from, to); }
};

struct Edge_hash
{
	size_t operator()(const struct Edge& ne) const
	{
		return ne.from + ne.to;
	}
};

struct DEdge
{
	int from, to, cap, flow;
};

class Graph
{
public:

    // Read-only query-service instrumentation; no orientation decisions change.
    vector<unsigned char> gated_marked;
    vector<int> gated_touched;
    void enable_notifications(int count) { gated_marked.assign(count,0); }
    void begin_update() { for(int v:gated_touched)gated_marked[v]=0;gated_touched.clear(); }
    void gated_touch(int v) { if(!gated_marked[v]){gated_marked[v]=1;gated_touched.push_back(v);} }
	int sz;
	int PA;
	int sum;
	int rd_PA;
	int inf = 100000000;
	vector<unordered_set<Edge, Edge_hash> > e;
	vector<int> rd;
	unordered_set<int> node;
	Graph() { sz = PA = sum = rd_PA = 0; };
	void resize(int n);
	void push_edge(int u, int v);
	void pull_edge(int u, int v);
	int search(int u, int v);
	void insert(int u, int v);
	void delet(int u, int v);
	void re(int u, int v);
	void display();
	void depict();
	bool check();

	void PA_decrease();
	bool ReTest(int testd);

	void input_optimal();
	void generate_insert(); //generate the edges to insert and delete
	int insert_num = 10000; vector<Edge> pend_insert;
	int count_edge();

	int n, m, s, t;
	vector<DEdge> De;
	vector<vector<int> > DG;
	vector<int> d, cur;
	vector<bool> vis;
	void make_edge(int from, int to, int cap);
	bool dinic_BFS();
	int dinic_DFS(int x, int a);
};

bool operator==(const struct Edge& X, const struct Edge& Y)
{
	return (Y.from == X.from) && (Y.to == X.to);
}

void Graph::resize(int n)
{
	sz = n;
	e.resize(sz + 1);
	rd.resize(sz + 1);
}

void Graph::push_edge(int u, int v)
{
	gated_touch(u); gated_touch(v);
	Edge ne(u, v);
	e[u].insert(ne);
	e[v].insert(ne);
	rd[v]++;
	if (rd[v] == PA) rd_PA++;
}

void Graph::pull_edge(int u, int v)
{
	gated_touch(u); gated_touch(v);
	Edge ne(u, v);
	auto it = e[u].find(ne);
	if (it != e[u].end())
	{
		e[u].erase(it);
		it = e[v].find(ne);
		e[v].erase(it);
		if (rd[v] == PA) rd_PA--;
		rd[v]--;
	}
	else
	{
		swap(ne.from, ne.to);
		it = e[u].find(ne);
		e[u].erase(it);
		it = e[v].find(ne);
		e[v].erase(it);
		if (rd[u] == PA) rd_PA--;
		rd[u]--;
	}
}

void Graph::re(int u, int v)
{
	gated_touch(u); gated_touch(v);
	Edge ne(u, v), to(v, u);
	auto it = e[u].find(ne);
	e[u].erase(it);
	e[u].insert(to);
	it = e[v].find(ne);
	e[v].erase(it);
	e[v].insert(to);
	if (rd[v] == PA) rd_PA--;
	rd[v]--;
	rd[u]++;
	if (rd[u] == PA) rd_PA++;
}

int Graph::search(int u, int v)
{
	//assert(u <= sz && v <= sz);
	Edge e1(u, v), e2(v, u);
	if (e[u].find(e1) != e[u].end())
		return 1;
	if (e[u].find(e2) != e[u].end())
		return 2;
	return 0;
}

void Graph::insert(int u, int v)
{
	if (rd[u] <= (PA - 1) || rd[v] <= (PA - 1))
	{
		if (rd[u] <= (PA - 2))
			push_edge(v, u);
		else if (rd[v] <= (PA - 2))
			push_edge(u, v);
		else
		{
			sum++;

			unordered_set<int> vis; unordered_map<int, int> p;
			queue<int> Q;
			if (rd[u] == (PA - 1)) { Q.push(u); vis.insert(u); }
			if (rd[v] == (PA - 1)) { Q.push(v); vis.insert(v); }
			int ans = -1;
			while (!Q.empty())
			{
				int now = Q.front(); Q.pop();
				if (rd[now] <= (PA - 2))
				{
					ans = now;
					break;
				}
				for (auto ne : e[now])
				{
					if (ne.from == now) continue;
					if (vis.count(ne.from)) continue;
					if (rd[ne.from] == PA) continue;
					p[ne.from] = now;
					vis.insert(ne.from);
					Q.push(ne.from);
				}
			}
			if (ans != -1)
			{
				int now = ans;
				while (p.find(now) != p.end())
				{
					re(now, p[now]);
					now = p[now];
				}
				int tnode = (now == u) ? v : u;
				push_edge(tnode, now);
			}
			else
			{
				if (rd[u] == PA - 1)
					push_edge(v, u);
				else
					push_edge(u, v);
			}
		}
	}
	else
	{
		sum++;

		unordered_set<int> vis; unordered_map<int, int> p;
		queue<int> Q;
		Q.push(u); vis.insert(u);
		Q.push(v); vis.insert(v);
		int ans = -1;
		while (!Q.empty())
		{
			int now = Q.front(); Q.pop();
			if (rd[now] == (PA - 1))
			{
				ans = now;
				break;
			}
			for (auto ne : e[now])
			{
				if (ne.from == now) continue;
				if (vis.find(ne.from) != vis.end()) continue;
				p[ne.from] = now;
				vis.insert(ne.from);
				Q.push(ne.from);
			}
		}
		if (ans != -1)
		{
			int now = ans;
			while (p.find(now) != p.end())
			{
				re(now, p[now]);
				now = p[now];
			}
			int tnode = (now == u) ? v : u;
			push_edge(tnode, now);
		}
		else
		{
			PA++;
			rd_PA = 0;
			push_edge(u, v);
		}
	}
}

void Graph::delet(int u, int v)
{
	int search_result = search(u, v);

	if (search_result == 2)
		swap(u, v);

	if (rd[v] == PA || rd[v] <= (PA - 2))
		pull_edge(u, v);
	else if (rd[v] == (PA - 1))
	{
		sum++;
		unordered_set<int> vis; unordered_map<int, int> p;
		queue<int> Q; Q.push(v); vis.insert(v);
		int ans = -1;
		while (!Q.empty())
		{
			int now = Q.front(); Q.pop();
			if (rd[now] == rd[v] + 1)
			{
				ans = now;
				break;
			}
			for (auto ne : e[now])
			{
				if (ne.to == now) continue;
				if (rd[ne.to] < (PA - 1)) continue;
				if (vis.find(ne.to) != vis.end()) continue;
				p[ne.to] = now;
				vis.insert(ne.to);
				Q.push(ne.to);
			}
		}

		if (ans != -1)
		{
			int now = ans;
			while (p.find(now) != p.end())
			{
				re(p[now], now);
				now = p[now];
			}
			pull_edge(u, v);
		}
		else
		{
			pull_edge(u, v);
		}
	}

	if (e[v].size() == 0)
		node.erase(v);
	if (e[u].size() == 0)
		node.erase(u);
	if (v == sz && e[sz].size() == 0)
	{
		int tarsz;
		for (tarsz = sz - 1; tarsz >= 0; tarsz--)
			if (e[tarsz].size() != 0)
				break;
		resize(tarsz);
	}

	if (rd_PA == 0)
	{
		PA_decrease();
	}
}

void Graph::PA_decrease()
{
	PA--;

	int try_num = 0;
	unordered_set<int> top;
	for (auto i : node)
		if (rd[i] == PA)
			top.insert(i);

	s = sz + 1, t = sz + 2;
	De.clear();
	DG.clear(); DG.resize(sz + 3);
	d.resize(sz + 3);
	vis.resize(sz + 3);

	for (auto i : node)
	{
		if (e[i].size() == 0) continue;
		if (rd[i] == PA)
			make_edge(s, i, 1);
		else if (rd[i] <= PA - 2)
			make_edge(i, t, PA - 1 - rd[i]);
	}
	int tag = De.size();
	for (auto i : node)
		for (auto ne : e[i])
			if (ne.from == i)
				make_edge(ne.to, ne.from, 1);

	int flow = 0;
	while (dinic_BFS())
	{
		cur.assign(sz + 3, 0);
		flow += dinic_DFS(s, inf);
	}

	int esize = De.size();
	for (int i = tag; i < esize; i += 2)
	{
		DEdge& ne = De[i];
		if (ne.flow)
			re(ne.to, ne.from);
	}

	rd_PA = 0;
	for (auto i : node)
	{
		if (rd[i] == PA)
			rd_PA++;
	}
	return;
}

void Graph::make_edge(int from, int to, int cap)
{
	De.push_back({ from, to, cap, 0 });
	DG[from].push_back(De.size() - 1);
	De.push_back({ to, from, 0, 0 });
	DG[to].push_back(De.size() - 1);
}

bool Graph::dinic_BFS()
{
	vis.assign(sz + 3, false); queue<int> Q; Q.push(s); d[s] = 0, vis[s] = true;
	while (!Q.empty())
	{
		int x = Q.front(); Q.pop();
		for (int i = 0; i < DG[x].size(); i++)
		{
			DEdge& ne = De[DG[x][i]];
			if (!vis[ne.to] && ne.cap > ne.flow) { vis[ne.to] = true; d[ne.to] = d[x] + 1; Q.push(ne.to); }
		}
	}
	return vis[t];
}

int Graph::dinic_DFS(int x, int a)
{
	if (x == t || a == 0) return a;
	int flow = 0, f;
	for (int& i = cur[x]; i < DG[x].size(); i++)
	{
		DEdge& ne = De[DG[x][i]];
		if (d[x] + 1 == d[ne.to] && (f = dinic_DFS(ne.to, min(a, ne.cap - ne.flow))) > 0)
		{
			ne.flow += f;
			De[DG[x][i] ^ 1].flow -= f;
			flow += f;
			a -= f;
			if (a == 0) break;
		}
	}
	return flow;
}

bool Graph::ReTest(int testd)
{
	s = sz + 1, t = sz + 2;
	De.clear();
	DG.clear(); DG.resize(sz + 3);
	d.resize(sz + 3);
	vis.resize(sz + 3);

	for (auto i : node)
	{
		if (e[i].size() == 0) continue;
		if (rd[i] > testd)
			make_edge(s, i, rd[i] - testd);
		else if (rd[i] < testd)
			make_edge(i, t, testd - rd[i]);
	}
	int tag = De.size();
	for (auto i : node)
		for (auto ne : e[i])
			if (ne.from == i)
				make_edge(ne.to, ne.from, 1);

	int flow = 0;
	while (dinic_BFS())
	{
		cur.assign(sz + 3, 0);
		flow += dinic_DFS(s, inf);
	}

	int esize = De.size();
	for (int i = tag; i < esize; i += 2)
	{
		DEdge& ne = De[i];
		if (ne.flow)
			re(ne.to, ne.from);
	}

	for (auto i : DG[s])
	{
		if (De[i].flow != De[i].cap)
			return false;
	}
	return true;
}


} // namespace pvldb2024
