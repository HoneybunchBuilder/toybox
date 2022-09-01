#include "imguisystem.h"

#include "imguicomponent.h"
#include "inputcomponent.h"
#include "profiling.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "tbimgui.h"
#include "tbvma.h"

bool create_imgui_system(ImGuiSystem *self, const ImGuiSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  TB_CHECK_RETURN(system_dep_count == 1,
                  "Different than expected number of system dependencies",
                  false);
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  // Find the render system
  RenderSystem *render_system = NULL;
  for (uint32_t i = 0; i < system_dep_count; ++i) {
    if (system_deps[i]->id == RenderSystemId) {
      render_system = (RenderSystem *)system_deps[i]->self;
      break;
    }
  }
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which imgui depends on", false);

  *self = (ImGuiSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
  };
  return true;
}

void destroy_imgui_system(ImGuiSystem *self) { *self = (ImGuiSystem){0}; }

void tick_imgui_system(ImGuiSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)output; // No output for this system
  TracyCZoneN(ctx, "ImGui System", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  // Find expected components
  uint32_t imgui_entity_count = 0;
  const EntityId *imgui_entities = NULL;
  const PackedComponentStore *imgui_comp_store = NULL;

  uint32_t input_entity_count = 0;
  const PackedComponentStore *input_comp_store = NULL;
  TB_CHECK(input->dep_set_count == 2, "Unexpected number of dependency sets");
  for (uint32_t dep_set_idx = 0; dep_set_idx < input->dep_set_count;
       ++dep_set_idx) {
    const SystemDependencySet *dep_set = &input->dep_sets[dep_set_idx];

    for (uint32_t col_idx = 0; col_idx < dep_set->column_count; ++col_idx) {
      const PackedComponentStore *column = &dep_set->columns[col_idx];
      if (column->id == ImGuiComponentId) {
        imgui_comp_store = column;
        imgui_entities = dep_set->entity_ids;
        imgui_entity_count = dep_set->entity_count;
      }

      if (column->id == InputComponentId) {
        input_comp_store = column;
        input_entity_count = dep_set->entity_count;
      }
    }
  }
  if (imgui_entity_count > 0) {
    TB_CHECK(imgui_entities, "Invalid input entities");
    TB_CHECK(imgui_comp_store, "Failed to find imgui component store");

    for (uint32_t entity_idx = 0; entity_idx < imgui_entity_count;
         ++entity_idx) {
      const ImGuiComponent *imgui =
          &((const ImGuiComponent *)imgui_comp_store->components)[entity_idx];

      igSetCurrentContext(imgui->context);

      // Apply this frame's input
      for (uint32_t input_idx = 0; input_idx < input_entity_count;
           ++input_idx) {
        const InputComponent *input =
            &((const InputComponent *)input_comp_store->components)[input_idx];
        for (uint32_t event_idx = 0; event_idx < input->event_count;
             ++event_idx) {
          const SDL_Event *event = &input->events[event_idx];

          // TODO: Feed event to imgui
          (void)event;
        }
      }

      // Apply basic IO
      ImGuiIO *io = igGetIO();
      io->DeltaTime = delta_seconds; // Note that ImGui expects seconds
      // TODO: Fetch this from the renderer
      io->DisplaySize = (ImVec2){1600.0f, 900.0f};

      igRender();

      ImDrawData *draw_data = igGetDrawData();
      TB_CHECK(draw_data, "Failed to retrieve draw data");

      // Send to render thread
      if (draw_data->Valid) {
        // Calculate how big the draw data is
        size_t imgui_size = 0;
        {
          const size_t idx_size =
              (size_t)draw_data->TotalIdxCount * sizeof(ImDrawIdx);
          const size_t vtx_size =
              (size_t)draw_data->TotalVtxCount * sizeof(ImDrawVert);
          // We know to use 8 for the alignment because the vertex
          // attribute layout starts with a float2
          const size_t alignment = 8;
          const size_t align_padding = idx_size % alignment;

          imgui_size = idx_size + align_padding + vtx_size;
        }

        if (imgui_size > 0) {
          // Make space for this on the next frame. For the host and the device
          VkBuffer tmp_host_buffer = VK_NULL_HANDLE;
          uint64_t offset = 0;
          void *tmp_ptr = NULL;
          if (!tb_rnd_sys_alloc_tmp_host_buffer(self->render_system, imgui_size,
                                                &tmp_host_buffer, &offset,
                                                &tmp_ptr)) {
            TracyCZoneEnd(ctx);
            return;
          }

          // Copy imgui mesh to the gpu driver controlled host buffer
          {
            size_t idx_size =
                (size_t)draw_data->TotalIdxCount * sizeof(ImDrawIdx);

            // We know to use 8 for the alignment because the vertex
            // attribute layout starts with a float2
            const size_t alignment = 8;
            size_t align_padding = idx_size % alignment;

            uint8_t *idx_dst = (uint8_t *)tmp_ptr;
            uint8_t *vtx_dst = idx_dst + idx_size + align_padding;

            // Organize all mesh data into a single cpu-side buffer
            for (int32_t i = 0; i < draw_data->CmdListsCount; ++i) {
              const ImDrawList *cmd_list = draw_data->CmdLists[i];

              size_t idx_byte_count =
                  (size_t)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
              size_t vtx_byte_count =
                  (size_t)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);

              SDL_memcpy(idx_dst, cmd_list->IdxBuffer.Data, idx_byte_count);
              SDL_memcpy(vtx_dst, cmd_list->VtxBuffer.Data, vtx_byte_count);

              idx_dst += idx_byte_count;
              vtx_dst += vtx_byte_count;
            }
          }

          // Instruct the render thread that it needs to upload the mesh to the
          // gpu

          // Send the render thread a draw instruction
        }
      }

      igNewFrame();
    }
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(imgui, ImGuiSystem, ImGuiSystemDescriptor)

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *input_desc) {
  desc->name = "ImGui";
  desc->size = sizeof(ImGuiSystem);
  desc->id = ImGuiSystemId;
  desc->desc = (InternalDescriptor)input_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 2;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {InputComponentId},
  };
  desc->deps[1] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {ImGuiComponentId},
  };
  desc->system_dep_count = 1;
  desc->system_deps[0] = RenderSystemId;
  desc->create = tb_create_imgui_system;
  desc->destroy = tb_destroy_imgui_system;
  desc->tick = tb_tick_imgui_system;
}
