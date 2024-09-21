import bpy
import os
import subprocess


class OpenEditorOperator(bpy.types.Operator):
    bl_idname = "tb.open_editor"
    bl_label = "Open In Editor"

    def execute(self, context):
        abs_path = os.path.abspath(context.scene.toybox.project_path)
        editor = context.scene.toybox.editor

        subprocess.Popen(editor + " .", cwd=abs_path,
                         shell=True, env=os.environ.copy())
        return {'FINISHED'}
