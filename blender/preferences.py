import bpy
import subprocess

# Access via
# some_prop = bpy.context.preferences.addons[__package__].preferences.some_prop


class TbCheckEnvironment(bpy.types.Operator):
    bl_idname = "tb.check_env"
    bl_label = "Check Environment"

    def check_git(self):
        return subprocess.run(["git", "--version"]).returncode

    def execute(self, context):
        res_str = "<NOT FOUND>"
        if self.check_git() is 0:
            res_str = "OK"

        # Write updates to preferences structure
        bpy.context.preferences.addons[__package__].preferences.has_git = res_str

        return {"FINISHED"}


class ToyboxPrefs(bpy.types.AddonPreferences):
    bl_idname = __package__
    has_git: bpy.props.StringProperty(name="Git")
    engine_root: bpy.props.StringProperty(name="Toybox Root", default="")
    editor: bpy.props.StringProperty(name="Editor", default="code")

    def draw(self, context):
        layout = self.layout
        layout.label(text="Prerequisites")
        row = layout.row()
        row.prop(self, "has_git")
        row.enabled = False
        layout.operator(TbCheckEnvironment.bl_idname)

        layout.label(text="Settings")
        layout.prop(self, "engine_root")
        layout.prop(self, "editor")


# def register():
# bpy.ops.tb.check_env()
