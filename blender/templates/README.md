# Blender Operator Templates

Not a great name but whatever

---
Each child tree of this directory is a structure that the blender python addon will copy and fill out when performing certain actions.

For example, when a user selects `New Project` the files in the `new_project` tree will be copied and instantiated.

Template .in files are in the python named string format so specific variables can be supplied by python's `format` method.