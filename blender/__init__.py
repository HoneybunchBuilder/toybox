from . import auto_load

bl_info = {
    "name": "toybox",
    "author": "Honeybunch",
    "description": "",
    "blender": (3, 4, 0),
    "version": (0, 0, 1),
    "location": "",
    "warning": "",
    "category": "Generic",
}


auto_load.init()


def register():
    auto_load.register()


def unregister():
    auto_load.unregister()
