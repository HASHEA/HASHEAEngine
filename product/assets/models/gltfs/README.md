# Sandbox glTF Samples

This directory stores the glTF sample assets currently used by `project/src/sandbox`.

Included samples:

- `Avocado`
- `BoomBox`
- `DamagedHelmet`
- `Sponza`

Each model directory keeps its `glTF` variant plus the upstream `README`/`LICENSE` metadata needed for local inspection.

The Sandbox smoke suite currently exercises these assets through:

- `AssetDatabase`
- `load_model_from_file()`
- `load_mesh_from_file()` on the smaller samples
- `make_ashasset_from_model()`
- `Scene::instantiate_model()`
- `Scene::instantiate_asset()`
