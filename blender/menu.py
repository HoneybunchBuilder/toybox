import bpy
from pathlib import Path
from shutil import copytree
from shutil import copy


def tb_use_addon(app_name, namespace, addon):
    return "tb_use_addon({namespace}_{app_name} {addon})\n".format(
        app_name=app_name, namespace=namespace, addon=addon
    )


def tb_new_proj_copy(src, dst, out_dir, app_name, namespace):
    addons = ""
    use_water_addon = True
    if use_water_addon:
        addons += tb_use_addon(app_name, namespace, "tb_water")
    else:
        addons = "\n"

    if ".in" in src and "tb_config.h.in" not in src:
        # Read file, instantiate the template and then write
        with open(src, "r") as src_file:
            contents = src_file.read()
            instance = contents.format(
                addons=addons, app_name=app_name, namespace=namespace
            )
            instance_dst = dst[:-3]
            with open(instance_dst, "w") as dst_file:
                dst_file.write(instance)
    else:
        # Just copy the file normally
        copy(src, dst)


def tb_create_new_project(out_dir, app_name, namespace):
    print("Creating new Toybox Project in: ", out_dir)

    template_dir = Path(__file__).resolve().parent / Path("templates/new_project")
    print("Copying template from: ", template_dir)

    copytree(
        template_dir,
        out_dir,
        dirs_exist_ok=True,
        copy_function=lambda src, dst: tb_new_proj_copy(
            src, dst, out_dir, app_name, namespace
        ),
    )


class TbNewProjectOperator(bpy.types.Operator):
    bl_idname = "tb.new_project"
    bl_label = "Create a new Tobox project"
    bl_options = {"REGISTER", "INTERNAL"}

    directory: bpy.props.StringProperty(default=str(Path.home()), subtype="DIR_PATH")
    app_name: bpy.props.StringProperty(default="my_cool_game")
    namespace: bpy.props.StringProperty(default="ntb")

    def execute(self, context):
        self.directory = bpy.path.abspath(self.directory)
        tb_create_new_project(self.directory, self.app_name, self.namespace)
        return {"FINISHED"}

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "app_name", text="Application Name")
        layout.prop(self, "namespace", text="Namespace")
        layout.prop(self, "directory", text="Directory")


class ToyboxMenu(bpy.types.Menu):
    bl_label = "Toybox"
    bl_idname = "OBJECT_MT_toybox_menu"

    def draw(self, context):
        layout = self.layout
        layout.operator(TbNewProjectOperator.bl_idname)
        # TODO:
        # Environment


def draw_item(self, context):
    layout = self.layout
    layout.menu(ToyboxMenu.bl_idname)


def register():
    bpy.types.TOPBAR_MT_editor_menus.append(draw_item)


def unregister():
    bpy.types.TOPBAR_MT_editor_menus.remove(draw_item)
