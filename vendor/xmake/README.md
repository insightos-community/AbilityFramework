# Bundled build recipes

This directory contains the dependency recipes used by the maintenance baseline.
They derive from [xmake-repo](https://github.com/xmake-io/xmake-repo/tree/c4a7b6909fc7e36aebd52888deab13b55f78a12b)
and include the baseline's version/hash additions and system build-tool adapters.
InsightOS removed internal download mirrors for this public snapshot. Public
upstream URLs, archive checksums, version declarations and existing notices remain.

Recipes are covered by [Apache-2.0](LICENSE.md). Downloaded libraries retain their
own licenses, declared by each recipe; this does not relicense those libraries.
Install cmake, ninja and pkg-config on PATH before configuring the project.
The default build requires no additional private recipe repository or builder image.
