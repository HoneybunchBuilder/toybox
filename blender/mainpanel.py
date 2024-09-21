import json
import os
import sys

import bpy

def parse_presets(self, context):
    abs_path = os.path.abspath(context.scene.toybox.project_path)
    json_path = os.path.join(abs_path, os.path.join('CMakePresets.json'))
    if not os.path.exists(json_path):
        return []

    host_platform = 'Windows'

    with open(json_path, 'r') as presets_json:
        presets = json.load(presets_json)

        editor_presets = []

        configure_presets = presets['configurePresets']
        for preset in configure_presets:
            if 'condition' in preset:
                condition = preset['condition']
                if 'type' in condition:
                    # Taking a short cut here...
                    if condition['type'] == 'equals':
                        if condition['rhs'] == host_platform:
                            editor_presets.append(
                                (preset['name'], preset['displayName'], ''))
                else:
                    editor_presets.append(
                        (preset['name'], preset['displayName'], ''))
        return editor_presets


class ToyboxSettings(bpy.types.PropertyGroup):
    project_path: bpy.props.StringProperty(name='Path', default='../')
    project_name: bpy.props.StringProperty(
        name='Name', default='toybox-game')
    editor: bpy.props.StringProperty(name='Editor', default='code')
    build_preset: bpy.props.EnumProperty(name='Preset', items=parse_presets)
    build_config: bpy.props.EnumProperty(name='Config', items=[
        ('debug', 'Debug', ''),
        ('relwithdebinfo', 'RelWithDebInfo', ''),
        ('release', 'Release', ''),
    ] 
    )

class OBJECT_PT_Toybox(bpy.types.Panel):
    bl_label = 'Toybox'
    bl_idname = 'OBJECT_PT_Toybox'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Toybox'
    bl_context = 'objectmode'

    @classmethod
    def poll(self, context):
        return context.object is not None

    def draw(self, context):
        layout = self.layout

        row = layout.row()
        row.prop(context.scene.toybox, 'project_path')
        row.prop(context.scene.toybox, 'project_name')

        row = layout.row()
        row.prop(context.scene.toybox, 'build_preset')
        row.prop(context.scene.toybox, 'build_config')

        row = layout.row()
        row.operator('tb.build')
        row.operator('tb.run')
        row.operator('tb.refresh_components')

        row = layout.row()
        row.prop(context.scene.toybox, 'editor')
        row.operator('tb.open_editor')

def register():
    bpy.types.Scene.toybox = bpy.props.PointerProperty(type=ToyboxSettings)
