import bpy
import sys
import json
import subprocess
import functools

class UpdateProps():
  comp_name = ''
  prop_name = ''
  meta_type = ''
  def __init__(self, comp_name, prop_name, meta_type):
    self.comp_name = comp_name
    self.prop_name = prop_name
    self.meta_type = meta_type

from . cmake import *
tb_components = []
tb_component_registry = []
tb_prop_groups = {}
tb_comp_props = {}
tb_comp_meta = {}

def component_items(self, context):
  return tb_components

def comp_draw(self, context):
  layout = self.layout
  class_name = 'tb_' + self.bl_label
  meta = tb_comp_meta[self.bl_label]
  obj = getattr(context.object, class_name)
  for name in meta:
    layout.prop(obj, name)

def comp_poll(self, context):
  name = self.bl_label
  return context is not None and name in context.object.tb_components

def comp_update(self, context):
  # Write the whole component
  prop_names = tb_comp_props[self.name]
  context.object[self.name] = {}
  for attr in [attr for attr in dir(self) if attr in prop_names]:
    context.object[self.name][attr] = getattr(self, attr)
  return

class TbRefreshComponents(bpy.types.Operator):
  bl_idname = 'tb.refresh_components'
  bl_label = 'Refresh Components'

  def execute(self, context):
    output_dir = get_out_dir(context)
    exe_path = get_exe(context, output_dir)
    if(exe_path is None or not os.path.exists(exe_path)):
      run_build(context)
    exe_path = get_exe(context, output_dir)
    if(exe_path is None or not os.path.exists(exe_path)):
      # Build failed?
      return {'FINISHED'}
    
    # Run the project with --info
    meta = subprocess.run(executable=exe_path, args=['--info'], shell=True, cwd=output_dir, stdout=subprocess.PIPE).stdout
    
    # Clear existing registries
    for key,val in tb_prop_groups.items():
      if hasattr(bpy.types.Object, key):
        delattr(bpy.types.Object, key)
        bpy.utils.unregister_class(val)
      
    for comp in tb_component_registry:
      if hasattr(bpy.types, comp):
        bpy.utils.unregister_class(comp)
    tb_component_registry.clear()
    tb_components.clear()
    tb_comp_props.clear()
    
     # parse output json to a reflection dict
    meta_json = json.loads(meta)

    # Register each component type
    for comp_name in meta_json:
      comp_meta = meta_json[comp_name]
      if comp_meta is None or comp_meta == 0:
          continue
      lower_name = comp_name.lower()

      tb_components.append((lower_name, comp_name, ''))
      prop_class_name = 'tb_' + comp_name
      panel_class_name = prop_class_name + 'Panel'
      prop_name = 'tb_' + lower_name
      idname = 'OBJECT_PT_tb_' + lower_name
      
      tb_comp_props[lower_name] = []
      tb_comp_meta[lower_name] = comp_meta

      prop_class = type(prop_class_name, 
                        (bpy.types.PropertyGroup,),
                        {})
      
      prop_class.__annotations__['name'] = bpy.props.StringProperty(name='name', default=lower_name)
      
      for meta_name in comp_meta:
        meta_val = comp_meta[meta_name]
        meta_type = meta_val[0]
        meta_params = meta_val[0:]
        
        tb_comp_props[lower_name].append(meta_name)

        if meta_type == 'bool':
          prop_class.__annotations__[meta_name] = bpy.props.BoolProperty(name=meta_name, default=False, update=comp_update)
        elif meta_type == 'float':
          flt_min = sys.float_info.min
          flt_max = sys.float_info.max
          if 'range' in meta_params:
            val_range = meta_params['range']
            flt_min = val_range[0]
            flt_max = val_range[1]
          prop_class.__annotations__[meta_name] = bpy.props.FloatProperty(name=meta_name, default=0.5, min=flt_min, max=flt_max, update=comp_update)
        else:
          prop_class.__annotations__[meta_name] = bpy.props.StringProperty(name=meta_name, default='static', update=comp_update)

      comp_class = type(panel_class_name, 
                    (bpy.types.Panel, ),
                    {
                     'bl_idname': idname,
                     'bl_parent_id': 'OBJECT_PT_tb_components_panel',
                     'bl_label': comp_name,
                     'bl_space_type': 'PROPERTIES',
                     'bl_region_type': 'WINDOW',
                     'bl_context': 'object', 
                     'draw': comp_draw,
                     'poll': classmethod(comp_poll),
                     }
                    )

      tb_component_registry.append(panel_class_name)
      tb_prop_groups[prop_class_name] = prop_class
      
      bpy.utils.register_class(prop_class)
      bpy.utils.register_class(comp_class)

      setattr(bpy.types.Object, prop_name, bpy.props.PointerProperty(name=lower_name, type=prop_class))

    return {'FINISHED'}

class TbComponents(bpy.types.PropertyGroup):
  comp_sel: bpy.props.EnumProperty(name='Type', items=component_items)

class TbComponentAddOperator(bpy.types.Operator):
  bl_idname = 'tb.component_add'
  bl_label = 'Add Component'
  
  component: bpy.props.EnumProperty(name='Type', items=component_items)
  
  def execute(self, context):
    if self.component in context.object.tb_components:
      print('Already have ', self.component)
    else:
      context.object.tb_components[self.component] = 1
    
    return {'FINISHED'}

class TbComponentRemoveOperator(bpy.types.Operator):
  bl_idname = 'tb.component_remove'
  bl_label = 'Remove Component'
  
  component: bpy.props.EnumProperty(name='Type', items=component_items)
  
  def execute(self, context):
    if self.component in context.object.tb_components:
      del context.object.tb_components[self.component]
    
    return {'FINISHED'}

class TbComponentsPanel(bpy.types.Panel):
    bl_idname = 'OBJECT_PT_tb_components_panel'
    bl_label = 'Toybox Components'
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = 'object'
    
    @classmethod
    def poll(self, context):
        return context.object is not None

    def draw(self, context):
        layout = self.layout
        
        if(len(tb_components) == 0):
          layout.label(text='Refresh Components to populate this panel')
          return

        layout.prop(context.object.tb_components, 'comp_sel')
        selection = context.object.tb_components.comp_sel
        if selection == '':
          return
        
        if selection in context.object.tb_components:
          op = layout.operator(TbComponentRemoveOperator.bl_idname)
        else:
          op = layout.operator(TbComponentAddOperator.bl_idname)
        op.component = selection

def register():
    bpy.types.Object.tb_components = bpy.props.PointerProperty(type=TbComponents)