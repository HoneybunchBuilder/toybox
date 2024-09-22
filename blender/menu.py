import bpy


class ToyboxMenu(bpy.types.Menu):
    bl_label = "Toybox"
    bl_idname = "OBJECT_MT_custom_menu"

    def draw(self, context):
        layout = self.layout

        layout.operator("wm.open_mainfile")
        # TODO:
        # New Project
        # Environment


def draw_item(self, context):
    layout = self.layout
    layout.menu(ToyboxMenu.bl_idname)


def register():
    bpy.types.TOPBAR_MT_editor_menus.append(draw_item)


def unregister():
    bpy.types.TOPBAR_MT_editor_menus.remove(draw_item)
