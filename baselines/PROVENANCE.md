# Baseline provenance and licenses

Upstream attribution is preserved below and in the included license files.
These names identify publicly available third-party software, not the authors
of this artifact. No blanket license overrides the rights of an upstream
component. `source_hashes.json` records the packaged source files.

## Udshp

The unweighted dynamic implementation comes from the author artifact linked by
[arXiv:2204.08106](https://arxiv.org/abs/2204.08106):
[original source folder](https://drive.google.com/drive/folders/1JOepPhEaHP8nlbf8wm0D7J5gw7o_ydMh).
The included `core/` files preserve the author update logic, replication rule,
priority updates, traversal, extraction, and memory behavior. Read-only
allocation/load accessors and the selected witness were exposed for result
validation. Profiling hooks compile to no-ops in the supplied build.

The adapter supplies explicit update IDs and query schedules, compressed answer
evidence, and integer-overflow admission checks. It does not replace the native
update or extraction algorithm. No license file was supplied with this source
snapshot; this artifact does not assign a new license to it.

## CQ

`cq/core.h` is a C++ reconstruction based on Chekuri and Quanrud,
[arXiv:2210.02611v1](https://arxiv.org/abs/2210.02611v1), Figure 2 and Sections 3.4
and 6. It is not author-provided code. Section 6 does not give complete deletion
pseudocode; this implementation explicitly schedules changed endpoint
directions. It must not be presented as the authors' original implementation
or as a measurement of their proved worst-case bound.

## Exact (LP)

`exact/upstream/exactLPSolver.py` is copied without modification from
[weightedDensestSubgraph](https://github.com/jayeshchoudhari/weightedDensestSubgraph),
revision `4b11f17a3b719a0574e771cc46f47aef385dbd3b`.
Its SHA-256 is
`594bad79672fd230b9358d2b18cb987cadbd12eccb0a63925d5adde1985343a4`.
The adapter checks this digest before and after execution, invokes the original
GLOP routine, and saves its returned set. The source's existing tolerance and
support-selection behavior are unchanged. No license file was supplied for
this component; a license in a different upstream subdirectory does not apply
automatically to this file.

## DI

`static/di/src/` contains the unchanged `hlpp.jl`, `utils.jl`, and `DHSG.jl` from
[local-DSHG](https://github.com/luotuoqingshan/local-DSHG), revision
`1be6f227ca483b2312dfd7cc2acaece50afebe38`. The adapter calls its density
improvement solver on each requested snapshot. The upstream MIT license is in
`static/di/LICENSE`.

## ExactFlow

`static/exactflow/` contains unchanged flow source from
[Densest-SWAMP](https://github.com/Vedangi/Densest-SWAMP), revision
`9a262789ad59b20dedb16cc105d087ae1fbc374d`. The adapter supplies the unweighted
full-hyperedge reward and calls the upstream solver. The MIT licenses for the
main code and its included flow support code are in `static/exactflow/LICENSE`
and `static/exactflow/include/LICENSE.txt`.

## ImprovedDynOpt

The source is derived from
[DynDeltaOrientation](https://github.com/DynGraphLab/DynDeltaOrientation),
revision `5ef684d0deb5a7cb6a2f8f79e88a137b3480112b`, under the included
`ordinary/improved_dynopt/LICENSE`.

The portable adaptation replaces unused graph-export infrastructure with a
minimal shell and exposes loads and assignments. Change notifications record
the vertices touched by an update without changing orientation decisions.
The experimental `fixed` variant also refreshes the maximum-degree vertex
count when the maximum degree reaches 1 or 0. The original and corrected
variants are both included, with `boundary_fix_*.patch` documenting that
correction. The scalar-query adapter and certificate gate are separate from
the upstream orientation algorithm.

## INS/DEL

The source comes from
[Pseudoarboricity-Computation](https://github.com/Flydragonet/Pseudoarboricity-Computation),
revision `4644e0dda057d5aa8446c80178161f7669aadcb6`, file
`Dynamic (INS, DEL)/main.cpp`.

The extraction keeps the `Edge` and `Graph` declarations and update/search/flow
methods, removing global file I/O and the random workload driver. The adapter
handles newly activated endpoints and includes touched-vertex notifications
and a native query candidate. `insdel_uninstrumented.h`, `notifications.patch`,
and `native_service.patch` document the separation. No license file was
provided by the pinned upstream source tree; no new license is assigned here.
