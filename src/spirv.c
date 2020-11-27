#include "spirv.h"
#include "../include/visky/vklite2.h"
#include <StandAlone/resource_limits_c.h>
#include <glslang/Include/glslang_c_interface.h>



VkShaderModule vkl_shader_compile(VklGpu* gpu, const char* code, VkShaderStageFlagBits stage)
{
    glslang_stage_t glslang_stage = GLSLANG_STAGE_VERTEX;
    switch (stage)
    {
    case VK_SHADER_STAGE_VERTEX_BIT:
        glslang_stage = GLSLANG_STAGE_VERTEX;
        break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        glslang_stage = GLSLANG_STAGE_FRAGMENT;
        break;
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        glslang_stage = GLSLANG_STAGE_GEOMETRY;
        break;
    case VK_SHADER_STAGE_COMPUTE_BIT:
        glslang_stage = GLSLANG_STAGE_COMPUTE;
        break;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
        glslang_stage = GLSLANG_STAGE_TESSCONTROL;
        break;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
        glslang_stage = GLSLANG_STAGE_TESSEVALUATION;
        break;
    default:
        log_error("unsupported shader stage");
        break;
    }
    ASSERT(glslang_stage != 0);
    const glslang_input_t input = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = glslang_stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_1,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_3,
        .code = code,
        .default_version = 100,
        .default_profile = GLSLANG_NO_PROFILE,
        .force_default_version_and_profile = false,
        .forward_compatible = false,
        .messages = GLSLANG_MSG_DEFAULT_BIT,
        .resource = glslang_default_resource(),
    };

    glslang_initialize_process();

    glslang_shader_t* shader = glslang_shader_create(&input);

    if (!glslang_shader_preprocess(shader, &input))
    {
        // use glslang_shader_get_info_log() and glslang_shader_get_info_debug_log()
    }

    if (!glslang_shader_parse(shader, &input))
    {
        // use glslang_shader_get_info_log() and glslang_shader_get_info_debug_log()
    }

    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT))
    {
        // use glslang_program_get_info_log() and glslang_program_get_info_debug_log();
    }

    glslang_program_SPIRV_generate(program, input.stage);

    if (glslang_program_SPIRV_get_messages(program))
    {
        log_debug("%s", glslang_program_SPIRV_get_messages(program));
    }

    glslang_shader_delete(shader);

    VkShaderModuleCreateInfo createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = glslang_program_SPIRV_get_size(program) * sizeof(unsigned int);
    createInfo.pCode = glslang_program_SPIRV_get_ptr(program);

    VkShaderModule module = {0};
    VkResult res = vkCreateShaderModule(gpu->device, &createInfo, NULL, &module);
    if (res != VK_SUCCESS)
    {
        log_error("unable to create shader module");
    }

    glslang_program_delete(program);

    return module;
}