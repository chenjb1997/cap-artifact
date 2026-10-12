"""Public dataset locations and the fixed preprocessing parameters."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "data" / "raw"
PROCESSED = ROOT / "data" / "processed"
QUARTER_MS = 90 * 24 * 60 * 60 * 1000
THREAD_SEED = 20261008
SAMPLE_SEEDS = {"GE": 20260928, "SO": 20260929, "TS": 20260930}

def cornell(name, drive_id, sha256, **kw):
    return dict(name=name, filename=name + ".tar.gz", kind="cornell",
                page=f"https://www.cs.cornell.edu/~arb/data/{name}/",
                url=f"https://drive.google.com/uc?export=download&id={drive_id}",
                drive_id=drive_id, sha256=sha256, **kw)

CATALOG = {
    "DW": cornell("DAWN", "1wGwoG7oBWnNN7J9TEpjqNpODbsYfMxp4", "f227d42a26e0c38b1a7bc618166dc33c11bb0388d5d7f7b28ba1985383125c3a", scale=1, window=1, rank=16),
    "MA": cornell("tags-math-sx", "1eDevpF6EZs19rLouNpiKGLIlFOLUfKKG", "1d57c2e7e4014396936fde21c6a222d8781b411a9bae504a2bebf4ed176cc90a", scale=QUARTER_MS, window=1, rank=5),
    "AU": cornell("tags-ask-ubuntu", "1tb1ZJlXEJnlRkXpTuBZlOqqsFknWCkUV", "8e29b3ab11871c78f0df86f514304bafdb976a5e2e32ea6e64683c0303421a95", scale=QUARTER_MS, window=1, rank=5),
    "GE": cornell("coauth-MAG-Geology", "14MOWsEJyNGiFKumvmgMcU1CqekDBayYV", "193d6f276191ad982958897cde94346c21e2fd0d9aec14f8f6583e2aab5cad24", scale=1, window=10, rank=25),
    "SO": cornell("tags-stack-overflow", "17XFOUNXR8Ecxhmt6YWMPt1uVr5yxiSz7", "60057242c0917015ea513dbd38957e38469dd2e51cf0a958d89e1a489fe36b78", scale=QUARTER_MS, window=1, rank=5),
    "TS": cornell("threads-stack-overflow", "1s66SSQpUzCzwaBTJ46av0NixzA-UTP9j", "8970102061e07b540bc81043f2c6941e88973c0a78e86783082277d1827e26e5", rank=25),
    "EM": dict(name="email-Eu-core", filename="email-Eu-core.txt.gz", kind="static_graph", page="https://snap.stanford.edu/data/email-Eu-core.html", url="https://snap.stanford.edu/data/email-Eu-core.txt.gz", sha256="4b47acdb80197b085fe63c819c357ae488131ee904ed93d1b219a68b0f9e245f", rank=2),
    "HT": dict(name="ca-HepTh", filename="ca-HepTh.txt.gz", kind="static_graph", page="https://snap.stanford.edu/data/ca-HepTh.html", url="https://snap.stanford.edu/data/ca-HepTh.txt.gz", sha256="7235c7e11887f8a32dfd03a3aa0c77efdf89d41591a959c7d6e4ca11b1299bb5", rank=2),
    "WV": dict(name="wiki-Vote", filename="wiki-Vote.txt.gz", kind="static_graph", page="https://snap.stanford.edu/data/wiki-Vote.html", url="https://snap.stanford.edu/data/wiki-Vote.txt.gz", sha256="7d3e53626e14b8b09fb3b396bece9d481ad606bd64ceab066349ff57d4ada7fc", rank=2),
    "BC": dict(name="soc-sign-bitcoinotc", filename="soc-sign-bitcoinotc.csv.gz", kind="bitcoin", page="https://snap.stanford.edu/data/soc-sign-bitcoin-otc.html", url="https://snap.stanford.edu/data/soc-sign-bitcoinotc.csv.gz", sha256="6424ac981dad3a019f697fc1b9fcd85c19d8d9f039797758e9ffb6fea100c373", rank=2),
    "AS": dict(name="as-caida", filename="as-caida.xz", kind="dynamic_graph", page="https://snap.stanford.edu/data/as-Caida.html", url="https://dyreach.taa.univie.ac.at/assets/data/snap/as-caida.xz", sha256="8ea084ca1e2c4de551c68a1a0e2a7d91e669c3d34375de904230d26b0e9deb19", rank=2),
    "SW": dict(name="link-dynamic-simplewiki", filename="link-dynamic-simplewiki.xz", kind="dynamic_graph", page="https://dyngraphlab.github.io/", url="https://dyreach.taa.univie.ac.at/assets/data/konect/link-dynamic-simplewiki.xz", sha256="d10ff6370d8fa5dd183c062633e774956f7085ba51e480ff21e29ae340c895ed", rank=2),
}
HYPER = ("DW", "MA", "AU", "GE", "SO", "TS4", "TS7", "TS")
GRAPHS = ("EM", "HT", "WV", "BC", "AS", "SW")
EXPECTED = {
    "DW": dict(n=2290, rank=16, updates=529412, queries=32, peak_edges=11544),
    "MA": dict(n=1627, rank=5, updates=611274, queries=29, peak_edges=21349),
    "AU": dict(n=3021, rank=5, updates=382007, queries=33, peak_edges=10458),
    "GE": dict(n=1087111, rank=25, updates=1471530, queries=210, peak_edges=409152),
    "SO": dict(n=49918, rank=5, updates=15897567, queries=35, peak_edges=351521),
    "TS4": dict(n=2260330, rank=4, updates=12151237, queries=12154, peak_edges=8100825),
    "TS7": dict(n=2317462, rank=7, updates=12837849, queries=12840, peak_edges=8558567),
    "TS": dict(n=2321751, rank=25, updates=12884130, queries=12887, peak_edges=8589420),
    "EM": dict(n=986, rank=2, updates=28032),
    "HT": dict(n=9875, rank=2, updates=32986),
    "WV": dict(n=7115, rank=2, updates=70381),
    "BC": dict(n=5881, rank=2, updates=44434),
    "AS": dict(n=31379, rank=2, updates=587677),
    "SW": dict(n=100312, rank=2, updates=1439148),
}

def source_for(name):
    return CATALOG["TS" if name in ("TS4", "TS7") else name]

def select(names):
    result = []
    for name in names:
        for expanded in HYPER + GRAPHS if name == "all" else HYPER if name == "hyper" else GRAPHS if name == "graphs" else (name,):
            if expanded not in HYPER + GRAPHS:
                raise ValueError(f"Unknown dataset: {expanded}")
            if expanded not in result:
                result.append(expanded)
    return result
