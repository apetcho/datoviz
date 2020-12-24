#include "../include/visky/visuals.h"
#include "../include/visky/canvas.h"
#include "../include/visky/graphics.h"



/*************************************************************************************************/
/*  Utils                                                                                        */
/*************************************************************************************************/

static bool _source_needs_binding(VklSourceKind source_kind)
{
    return source_kind == VKL_SOURCE_UNIFORM || //
           source_kind == VKL_SOURCE_STORAGE;
}



static bool _uniform_source_is_immediate(VklSource* source)
{
    ASSERT(source != NULL);
    if (source->source_kind != VKL_SOURCE_UNIFORM)
        return false;
    return (source->flags & VKL_SOURCE_FLAG_IMMEDIATE) != 0;
}



static VklSourceKind _get_source_kind(VklSourceType type)
{
    switch (type)
    {
    case VKL_SOURCE_TYPE_MVP:
    case VKL_SOURCE_TYPE_VIEWPORT:
    case VKL_SOURCE_TYPE_PARAM:
        return VKL_SOURCE_UNIFORM;
        break;

    case VKL_SOURCE_TYPE_VERTEX:
        return VKL_SOURCE_VERTEX;

    case VKL_SOURCE_TYPE_INDEX:
        return VKL_SOURCE_INDEX;

    case VKL_SOURCE_TYPE_IMAGE:
    case VKL_SOURCE_TYPE_COLOR_TEXTURE:
    case VKL_SOURCE_TYPE_FONT_ATLAS:
        return VKL_SOURCE_TEXTURE_2D;

    case VKL_SOURCE_TYPE_VOLUME:
        return VKL_SOURCE_TEXTURE_3D;

    default:
        log_error("source type %d not yet supported", type);
        return VKL_SOURCE_TYPE_NONE;
        break;
    }
}



static bool _source_is_texture(VklSourceKind source_kind)
{
    return source_kind == VKL_SOURCE_TEXTURE_1D || //
           source_kind == VKL_SOURCE_TEXTURE_2D || //
           source_kind == VKL_SOURCE_TEXTURE_3D;
}



static bool _source_is_buffer(VklSourceKind source_kind)
{
    return source_kind == VKL_SOURCE_UNIFORM || source_kind == VKL_SOURCE_STORAGE ||
           source_kind == VKL_SOURCE_VERTEX || source_kind == VKL_SOURCE_INDEX;
}



static VklBindings* _get_bindings(VklVisual* visual, VklSource* source)
{
    ASSERT(source != NULL);
    if (source->pipeline == VKL_PIPELINE_GRAPHICS)
        return &visual->bindings[source->pipeline_idx];
    else if (source->pipeline == VKL_PIPELINE_COMPUTE)
        return &visual->bindings_comp[source->pipeline_idx];
    return NULL;
}



static void _set_source_bindings(VklVisual* visual, VklSource* source)
{
    // Set bindings except for VERTEX and INDEX sources.
    if (_source_needs_binding(source->source_kind))
    {
        VklBindings* bindings = _get_bindings(visual, source);
        vkl_bindings_buffer(bindings, source->slot_idx, source->u.br);

        // Share the source's buffer regions with other pipelines.
        for (uint32_t i = 0; i < source->other_count; i++)
        {
            vkl_bindings_buffer(
                &visual->bindings[source->other_idxs[i]], source->slot_idx, source->u.br);
        }
    }
}



static uint32_t _get_buffer_idx(VklSource* source)
{
    switch (source->source_kind)
    {
    case VKL_SOURCE_VERTEX:
        return VKL_DEFAULT_BUFFER_VERTEX;
    case VKL_SOURCE_INDEX:
        return VKL_DEFAULT_BUFFER_INDEX;
    case VKL_SOURCE_UNIFORM:
        return (source->flags & VKL_SOURCE_FLAG_IMMEDIATE) != 0
                   ? VKL_DEFAULT_BUFFER_UNIFORM_MAPPABLE
                   : VKL_DEFAULT_BUFFER_UNIFORM;
    case VKL_SOURCE_STORAGE:
        return VKL_DEFAULT_BUFFER_STORAGE;
    default:
        log_error("buffer idx not found");
        break;
    }
    return 0;
}



static uint32_t _get_texture_ndims(VklSourceKind source_kind)
{
    uint32_t ndims = 1;
    if (source_kind == VKL_SOURCE_TEXTURE_2D)
        ndims = 2;
    if (source_kind == VKL_SOURCE_TEXTURE_3D)
        ndims = 3;
    return ndims;
}



static VkFormat _get_texture_format(VklVisual* visual, VklSource* source)
{
    ASSERT(source != NULL);
    ASSERT(_source_is_texture(source->source_kind));
    VklDataType dtype = VKL_DTYPE_NONE;

    for (uint32_t i = 0; i < visual->prop_count; i++)
    {
        if (visual->props[i].source == source)
        {
            // Check that there is only 1 prop associated to the texture source.
            if (dtype != VKL_DTYPE_NONE)
                log_error("multiple texture props not supported");
            dtype = visual->props[i].dtype;
        }
    }

    ASSERT(dtype != VKL_DTYPE_NONE);
    VkFormat format = VK_FORMAT_UNDEFINED;
    switch (dtype)
    {

    // 8 bit
    case VKL_DTYPE_CHAR:
        format = VK_FORMAT_R8_UNORM;
        break;

    case VKL_DTYPE_CVEC3:
        format = VK_FORMAT_R8G8B8_UNORM;
        break;

    case VKL_DTYPE_CVEC4:
        format = VK_FORMAT_R8G8B8A8_UNORM;
        break;


    // 16 bit signed
    case VKL_DTYPE_SHORT:
        format = VK_FORMAT_R16_SNORM;
        break;

    case VKL_DTYPE_SVEC3:
        format = VK_FORMAT_R16G16B16_SNORM;
        break;

    case VKL_DTYPE_SVEC4:
        format = VK_FORMAT_R16G16B16A16_SNORM;
        break;


    // 16 bit unsigned
    case VKL_DTYPE_USHORT:
        format = VK_FORMAT_R16_UNORM;
        break;

    case VKL_DTYPE_USVEC3:
        format = VK_FORMAT_R16G16B16_UNORM;
        break;

    case VKL_DTYPE_USVEC4:
        format = VK_FORMAT_R16G16B16A16_UNORM;
        break;


    default:
        break;
    }
    if (format == VK_FORMAT_UNDEFINED)
        log_error("unsupported texture format for dtype %d", dtype);
    return format;
}



/*************************************************************************************************/
/*  Functions                                                                                    */
/*************************************************************************************************/

VklVisual vkl_visual(VklCanvas* canvas)
{
    ASSERT(canvas != NULL);

    VklVisual visual = {0};
    visual.canvas = canvas;

    // Default callbacks.
    visual.callback_fill = _default_visual_fill;
    visual.callback_bake = _default_visual_bake;

    obj_created(&visual.obj);
    return visual;
}



void vkl_visual_destroy(VklVisual* visual)
{
    ASSERT(visual != NULL);

    // Free the props.
    for (uint32_t i = 0; i < visual->prop_count; i++)
    {
        vkl_array_destroy(&visual->props[i].arr_orig);
        vkl_array_destroy(&visual->props[i].arr_trans);
    }

    // Free the data sources.
    for (uint32_t i = 0; i < visual->source_count; i++)
        vkl_array_destroy(&visual->sources[i].arr);

    obj_destroyed(&visual->obj);
}



/*************************************************************************************************/
/*  Visual creation                                                                              */
/*************************************************************************************************/

void vkl_visual_source(
    VklVisual* visual, VklSourceType source_type, VklPipelineType pipeline, uint32_t pipeline_idx,
    uint32_t slot_idx, VkDeviceSize item_size, int flags)
{
    ASSERT(visual != NULL);
    ASSERT(visual->source_count < VKL_MAX_VISUAL_SOURCES);
    ASSERT(vkl_bake_source(visual, source_type, pipeline_idx) == NULL);

    VklSource source = {0};
    source.obj.type = VKL_OBJECT_TYPE_SOURCE;
    source.obj.status = VKL_OBJECT_STATUS_INIT;
    source.source_type = source_type;
    source.source_kind = _get_source_kind(source_type);
    source.pipeline = pipeline;
    source.pipeline_idx = pipeline_idx;
    source.slot_idx = slot_idx;
    source.flags = flags;

    if (source.source_kind < VKL_SOURCE_TEXTURE_1D)
        source.arr = vkl_array_struct(0, item_size);
    else
    {
        // Textures.
        uint32_t ndims = _get_texture_ndims(source.source_kind);
        source.arr = vkl_array_3D(ndims, 0, 0, 0, item_size);
    }

    // source origin (GPU object) not set yet
    source.origin = VKL_SOURCE_ORIGIN_NONE;

    // NOTE: exception for INDEX source, most frequently automatically handled by the library
    if (source.source_kind == VKL_SOURCE_INDEX)
    {
        source.origin = VKL_SOURCE_ORIGIN_LIB;
        source.obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
    }
    visual->sources[visual->source_count++] = source;
}



void vkl_visual_source_share(
    VklVisual* visual, VklSourceType source_type, uint32_t pipeline_idx, uint32_t other_idx)
{
    ASSERT(visual != NULL);
    VklSource* source = vkl_bake_source(visual, source_type, pipeline_idx);
    ASSERT(source != NULL);
    source->other_idxs[source->other_count++] = other_idx;
}



void vkl_visual_prop(
    VklVisual* visual, VklPropType prop_type, uint32_t prop_idx, VklDataType dtype,
    VklSourceType source_type, uint32_t pipeline_idx)
{
    ASSERT(visual != NULL);
    ASSERT(visual->prop_count < VKL_MAX_VISUAL_PROPS);

    VklProp prop = {0};

    prop.prop_type = prop_type;
    prop.prop_idx = prop_idx;
    prop.dtype = dtype;
    prop.source = vkl_bake_source(visual, source_type, pipeline_idx);
    if (prop.source == NULL)
    {
        log_error("source of type %d #%d not found", source_type, pipeline_idx);
    }
    ASSERT(prop.source != NULL);

    // NOTE: we do not use prop arrays for texture sources at the moment
    if (prop.source->source_kind < VKL_SOURCE_TEXTURE_1D)
        prop.arr_orig = vkl_array(0, prop.dtype);

    visual->props[visual->prop_count++] = prop;
}



void vkl_visual_prop_default(
    VklVisual* visual, VklPropType prop_type, uint32_t prop_idx, void* default_value)
{
    ASSERT(visual != NULL);
    VklProp* prop = vkl_bake_prop(visual, prop_type, prop_idx);
    ASSERT(prop != NULL);
    prop->default_value = default_value;
}



void vkl_visual_prop_copy(
    VklVisual* visual, VklPropType prop_type, uint32_t prop_idx, //
    uint32_t field_idx, VkDeviceSize offset, VklArrayCopyType copy_type, uint32_t reps)
{
    ASSERT(visual != NULL);
    VklProp* prop = vkl_bake_prop(visual, prop_type, prop_idx);
    ASSERT(prop != NULL);

    prop->field_idx = field_idx;
    prop->offset = offset;
    prop->copy_type = copy_type;
    prop->reps = reps;
}



void vkl_visual_graphics(VklVisual* visual, VklGraphics* graphics)
{
    ASSERT(visual != NULL);
    ASSERT(graphics != NULL);
    ASSERT(is_obj_created(&graphics->obj));
    if (visual->graphics_count >= VKL_MAX_GRAPHICS_PER_VISUAL)
    {
        log_error("maximum number of graphics per visual reached");
        return;
    }
    visual->graphics[visual->graphics_count] = graphics;
    visual->bindings[visual->graphics_count] =
        vkl_bindings(&graphics->slots, visual->canvas->swapchain.img_count);
    visual->graphics_count++;
}



void vkl_visual_compute(VklVisual* visual, VklCompute* compute)
{
    ASSERT(visual != NULL);
    ASSERT(compute != NULL);
    ASSERT(is_obj_created(&compute->obj));
    if (visual->compute_count >= VKL_MAX_COMPUTES_PER_VISUAL)
    {
        log_error("maximum number of computes per visual reached");
        return;
    }
    visual->computes[visual->compute_count++] = compute;
}



/*************************************************************************************************/
/*  User-facing functions                                                                        */
/*************************************************************************************************/

void vkl_visual_group(VklVisual* visual, uint32_t group_idx, uint32_t size)
{
    ASSERT(visual != NULL);
    if (group_idx >= VKL_MAX_VISUAL_GROUPS)
    {
        log_error("maximum number of groups reached");
        return;
    }
    visual->group_count = MAX(visual->group_count, group_idx + 1);
    visual->group_sizes[group_idx] = size;
}



void vkl_visual_data(
    VklVisual* visual, VklPropType prop_type, uint32_t idx, uint32_t count, const void* data)
{
    ASSERT(visual != NULL);
    vkl_visual_data_partial(visual, prop_type, idx, 0, count, count, data);
}



void vkl_visual_data_partial(
    VklVisual* visual, VklPropType prop_type, uint32_t idx, //
    uint32_t first_item, uint32_t item_count, uint32_t data_item_count, const void* data)
{
    ASSERT(visual != NULL);
    uint32_t count = first_item + item_count;
    ASSERT(count > 0);
    ASSERT(data_item_count > 0);

    // Get the associated prop.
    VklProp* prop = vkl_bake_prop(visual, prop_type, idx);
    ASSERT(prop != NULL);

    // Make sure the array has the right size.
    vkl_array_resize(&prop->arr_orig, count);

    // Copy the specified array to the prop array.
    vkl_array_data(&prop->arr_orig, first_item, item_count, data_item_count, data);

    // Get the associated source.
    VklSource* source = prop->source;
    ASSERT(source != NULL);
    if (source != NULL)
    {
        log_trace("source type %d #%d handled by lib", source->source_type, source->pipeline_idx);
        source->origin = VKL_SOURCE_ORIGIN_LIB;
        source->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
        visual->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
    }
}



static VklSource* _assert_source_exists(VklVisual* visual, VklSourceType source_type, uint32_t idx)
{
    VklSource* source = vkl_bake_source(visual, source_type, idx);

    // Check if the requested source is not a shared source.
    if (source == NULL)
    {
        for (uint32_t i = 0; i < visual->source_count; i++)
        {
            for (uint32_t j = 0; j < visual->sources[i].other_count; j++)
            {
                if (visual->sources[i].other_idxs[j] == idx)
                {
                    source = &visual->sources[i];
                    break;
                }
            }
        }
    }

    if (source == NULL)
    {
        log_error("source of type %d #%d not found", source_type, idx);
        return NULL;
    }
    ASSERT(source != NULL);
    return source;
}

void vkl_visual_data_buffer(
    VklVisual* visual, VklSourceType source_type, uint32_t idx, //
    uint32_t first_item, uint32_t item_count, uint32_t data_item_count, const void* data)
{
    ASSERT(visual != NULL);
    uint32_t count = first_item + item_count;
    ASSERT(count > 0);
    ASSERT(data_item_count > 0);

    // Get the associated source.
    VklSource* source = _assert_source_exists(visual, source_type, idx);

    // Make sure the array has the right size.
    vkl_array_resize(&source->arr, count);

    // Copy the specified array to the prop array.
    vkl_array_data(&source->arr, first_item, item_count, data_item_count, data);

    source->origin = VKL_SOURCE_ORIGIN_NOBAKE;
    source->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
    visual->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
}



void vkl_visual_data_texture(
    VklVisual* visual, VklPropType prop_type, uint32_t idx, //
    uint32_t width, uint32_t height, uint32_t depth, const void* data)
{
    ASSERT(visual != NULL);
    uint32_t count = width * height * depth;
    ASSERT(count > 0);

    // Get the associated prop.
    VklProp* prop = vkl_bake_prop(visual, prop_type, idx);
    ASSERT(prop != NULL);

    // Get the associated source.
    VklSource* source = prop->source;
    ASSERT(source != NULL);

    // NOTE: with array 3D props, the data is put directly to the source and not to the prop.
    // Make sure the array has the right size.
    vkl_array_reshape(&source->arr, width, height, depth);

    // Copy the specified array to the prop array.
    vkl_array_data(&source->arr, 0, count, count, data);

    source->origin = VKL_SOURCE_ORIGIN_NOBAKE;
    source->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
    visual->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
}



// Means that no data updates will be done by visky, it is up to the user to update the bound
// buffer
void vkl_visual_buffer(
    VklVisual* visual, VklSourceType source_type, uint32_t idx, VklBufferRegions br)
{
    ASSERT(visual != NULL);
    ASSERT(visual != NULL);
    VklSource* source = _assert_source_exists(visual, source_type, idx);
    ASSERT(source != NULL);

    VkDeviceSize size = br.size;
    ASSERT(size > 0);
    ASSERT(br.buffer != VK_NULL_HANDLE);

    source->u.br = br;
    source->origin = VKL_SOURCE_ORIGIN_USER;
    source->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
    visual->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;

    // Set the pipeline bindings with the source buffer.
    _set_source_bindings(visual, source);
}



void vkl_visual_texture(
    VklVisual* visual, VklSourceType source_type, uint32_t idx, VklTexture* texture)
{
    ASSERT(visual != NULL);
    ASSERT(visual != NULL);
    VklSource* source = _assert_source_exists(visual, source_type, idx);
    ASSERT(source != NULL);
    ASSERT(texture != NULL);

    source->u.tex = texture;
    source->origin = VKL_SOURCE_ORIGIN_USER;
    source->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;
    visual->obj.status = VKL_OBJECT_STATUS_NEED_UPDATE;

    VklBindings* bindings = _get_bindings(visual, source);
    ASSERT(texture->image != NULL);
    ASSERT(texture->sampler != NULL);
    vkl_bindings_texture(bindings, source->slot_idx, texture);
}



/*************************************************************************************************/
/*  Visual events                                                                                */
/*************************************************************************************************/

void vkl_visual_callback_transform(VklVisual* visual, VklVisualDataCallback callback)
{
    ASSERT(visual != NULL);
    visual->callback_transform = callback;
}



void vkl_visual_callback_bake(VklVisual* visual, VklVisualDataCallback callback)
{
    ASSERT(visual != NULL);
    visual->callback_bake = callback;
}



void vkl_visual_fill_callback(VklVisual* visual, VklVisualFillCallback callback)
{
    ASSERT(visual != NULL);
    VklCanvas* canvas = visual->canvas;
    ASSERT(canvas != NULL);
    visual->callback_fill = callback;
}



void vkl_visual_fill_event(
    VklVisual* visual, VkClearColorValue clear_color, VklCommands* cmds, uint32_t cmd_idx,
    VklViewport viewport, void* user_data)
{
    // Called in a REFILL canvas callback.
    // log_trace("visual %d fill event %d", visual, cmd_idx);

    ASSERT(visual != NULL);
    ASSERT(visual->callback_fill != NULL);

    VklVisualFillEvent ev = {0};
    ev.clear_color = clear_color;
    ev.cmds = cmds;
    ev.cmd_idx = cmd_idx;
    ev.viewport = viewport;
    ev.user_data = user_data;

    visual->callback_fill(visual, ev);
}



void vkl_visual_fill_begin(VklCanvas* canvas, VklCommands* cmds, uint32_t idx)
{
    ASSERT(canvas != NULL);
    vkl_cmd_begin(cmds, idx);
    vkl_cmd_begin_renderpass(cmds, idx, &canvas->renderpass, &canvas->framebuffers);
}



void vkl_visual_fill_end(VklCanvas* canvas, VklCommands* cmds, uint32_t idx)
{
    ASSERT(canvas != NULL);
    vkl_cmd_end_renderpass(cmds, idx);
    vkl_cmd_end(cmds, idx);
}



/*************************************************************************************************/
/*  Baking helpers                                                                               */
/*************************************************************************************************/

VklSource* vkl_bake_source(VklVisual* visual, VklSourceType source_type, uint32_t pipeline_idx)
{
    ASSERT(visual != NULL);
    VklSource* source = NULL;
    VklSource* out = NULL;
    for (uint32_t i = 0; i < visual->source_count; i++)
    {
        source = &visual->sources[i];
        if (source->source_type == source_type && source->pipeline_idx == pipeline_idx)
        {
            ASSERT(out == NULL);
            out = source;
        }
    }
    return out;
}



VklProp* vkl_bake_prop(VklVisual* visual, VklPropType prop_type, uint32_t idx)
{
    ASSERT(visual != NULL);
    for (uint32_t i = 0; i < visual->prop_count; i++)
    {
        if (visual->props[i].prop_type == prop_type && visual->props[i].prop_idx == idx)
            return &visual->props[i];
    }
    log_error("prop with type %d #%d not found", prop_type, idx);
    return NULL;
}



void* vkl_bake_prop_item(VklProp* prop, uint32_t idx)
{
    ASSERT(prop != NULL);
    void* res = prop->default_value;
    if (idx < prop->arr_orig.item_count)
        res = vkl_array_item(&prop->arr_orig, idx);
    return res;
}



uint32_t vkl_bake_max_prop_size(VklVisual* visual, VklSource* source)
{
    ASSERT(visual != NULL);
    ASSERT(source != NULL);

    VklProp* prop = NULL;
    VklArray* arr = NULL;
    uint32_t item_count = 0;
    for (uint32_t i = 0; i < visual->prop_count; i++)
    {
        prop = &visual->props[i];
        if (prop->source == source)
        {
            arr = &prop->arr_orig;
            ASSERT(arr != NULL);
            item_count = MAX(item_count, arr->item_count * MAX(1, prop->reps));
        }
    }
    return item_count;
}



void vkl_bake_prop_copy(VklVisual* visual, VklProp* prop)
{
    ASSERT(prop != NULL);

    VklSource* source = prop->source;
    ASSERT(source != NULL);

    VkDeviceSize col_size = _get_dtype_size(prop->dtype);
    ASSERT(col_size > 0);

    if (prop->arr_orig.data == NULL)
    {
        log_error("visual prop %d #%d not set", prop->prop_type, prop->prop_idx);
        return;
    }

    ASSERT(prop->arr_orig.data != NULL);
    ASSERT(source->arr.data != NULL);
    ASSERT(prop->arr_orig.item_count <= source->arr.item_count);

    // log_debug(
    //     "copy %d prop offset %d size %d into source size %d", //
    //     item_count, prop->offset, col_size, source->arr.item_size);
    vkl_array_column(
        &source->arr, prop->offset, col_size, 0, source->arr.item_count, //
        prop->arr_orig.item_count, prop->arr_orig.data, prop->copy_type, prop->reps);
}



void vkl_bake_source_alloc(VklVisual* visual, VklSource* source, uint32_t count)
{
    ASSERT(visual != NULL);
    ASSERT(source != NULL);

    // Resize the source source.
    log_trace(
        "alloc %d elements for source %d #%d", count, source->source_type, source->pipeline_idx);
    VklArray* arr = &source->arr;
    ASSERT(is_obj_created(&arr->obj));
    vkl_array_resize(arr, count);

    // if (source->source_kind == VKL_SOURCE_VERTEX)
    //     visual->vertex_count = count;
    // else if (source->source_kind == VKL_SOURCE_INDEX)
    //     visual->index_count = count;
}



void vkl_bake_source_fill(VklVisual* visual, VklSource* source)
{
    ASSERT(visual != NULL);
    ASSERT(source != NULL);

    VklProp* prop = NULL;
    // Copy all associated props to the source array.
    for (uint32_t i = 0; i < visual->prop_count; i++)
    {
        prop = &visual->props[i];
        if (prop->source == source)
            vkl_bake_prop_copy(visual, prop);
    }
}



void vkl_visual_buffer_alloc(VklVisual* visual, VklSource* source)
{
    ASSERT(visual != NULL);
    ASSERT(source != NULL);
    VklContext* ctx = visual->canvas->gpu->context;
    VklCanvas* canvas = visual->canvas;

    ASSERT(source->source_kind < VKL_SOURCE_TEXTURE_1D);
    ASSERT(source->arr.item_size > 0);

    uint32_t count = source->arr.item_count;
    ASSERT(count > 0);

    // Allocate the buffer if it doesn't exist yet, or if it is not large enough.
    if (source->u.br.buffer == VK_NULL_HANDLE || source->u.br.size < count * source->arr.item_size)
    {
        VkDeviceSize size = source->u.br.size * 2;
        if (size > 0)
            while (size < count * source->arr.item_size)
                size *= 2;
        else
            size = count * source->arr.item_size;
        ASSERT(size >= count * source->arr.item_size);
        if (source->u.br.size > 0)
            log_debug(
                "need to reallocate new buffer region to fit %d elements (%d bytes)", count, size);
        else
            log_debug(
                "need to allocate new buffer region to fit %d elements (%d bytes)", count, size);

        // Number of buffers: 1, unless using _immediate upload.
        uint32_t buf_count =
            _uniform_source_is_immediate(source) ? canvas->swapchain.img_count : 1;
        source->u.br = vkl_ctx_buffers(ctx, _get_buffer_idx(source), buf_count, size);

        // Set the pipeline bindings with the source buffer.
        _set_source_bindings(visual, source);
    }
    ASSERT(source->u.br.buffer != VK_NULL_HANDLE);
}



void vkl_visual_texture_alloc(VklVisual* visual, VklSource* source)
{
    ASSERT(visual != NULL);
    ASSERT(source != NULL);
    VklContext* ctx = visual->canvas->gpu->context;

    ASSERT(_source_is_texture(source->source_kind));

    // Find the numbe of dimensions.
    uint32_t ndims = _get_texture_ndims(source->source_kind);
    uvec3 shape = {source->arr.shape[0], source->arr.shape[1], source->arr.shape[2]};
    ASSERT(shape[0] > 0);
    ASSERT(shape[1] > 0);
    ASSERT(shape[2] > 0);

    // Find the texture format.
    VkFormat format = _get_texture_format(visual, source);
    ASSERT(format != VK_FORMAT_UNDEFINED);

    // Allocate the texture if it doesn't exist yet, or if it is not large enough.
    VklTexture* tex = source->u.tex;
    if (tex == NULL ||                   //
        tex->image->width < shape[0] ||  //
        tex->image->height < shape[1] || //
        tex->image->depth < shape[2])    //
    {
        if (tex == NULL)
        {
            log_debug(
                "need to create new texture with shape %dx%dx%d", //
                shape[0], shape[1], shape[2]);
            tex = source->u.tex = vkl_ctx_texture(ctx, ndims, shape, format);
        }
        else
        {
            log_debug(
                "need to resize texture to new shape %dx%dx%d", //
                shape[0], shape[1], shape[2]);
            vkl_texture_resize(source->u.tex, shape);
        }
        ASSERT(tex != NULL);

        // Set bindings.
        VklBindings* bindings = _get_bindings(visual, source);
        vkl_bindings_texture(bindings, source->slot_idx, tex);
    }
    ASSERT(source->u.tex != NULL);
}



/*************************************************************************************************/
/*  Data update                                                                                  */
/*************************************************************************************************/

void vkl_visual_update(
    VklVisual* visual, VklViewport viewport, VklDataCoords coords, const void* user_data)
{
    ASSERT(visual != NULL);
    log_trace("visual update");

    VklVisualDataEvent ev = {0};
    ev.viewport = viewport;
    ev.coords = coords;
    ev.user_data = user_data;

    if (visual->callback_transform != NULL)
    {
        log_trace("visual transform callback");
        // This callback updates some props data_trans
        visual->callback_transform(visual, ev);
    }

    if (visual->callback_bake != NULL)
    {
        log_trace("visual bake callback");

        // This callback does the following:
        // 1. Determine vertex count and index count
        // 2. Resize the VERTEX and INDEX array sources accordingly.
        // 3. Possibly resize other sources.
        // 4. Take the props and fill the array sources.
        visual->callback_bake(visual, ev);
    }
    // NOTE: we bake the UNIFORM sources here.
    _bake_uniforms(visual);


    // Here, we assume that all sources are correctly allocated, which includes VERTEX and INDEX
    // arrays, and that they have their data ready for upload.
    // Upload the buffers and textures
    VklSource* source = NULL;
    VklArray* arr = NULL;
    VklBufferRegions* br = NULL;
    VklCanvas* canvas = visual->canvas;
    VklTexture* texture = NULL;
    VklContext* ctx = visual->canvas->gpu->context;
    bool to_upload = false;
    for (uint32_t i = 0; i < visual->source_count; i++)
    {
        source = &visual->sources[i];
        if (source->origin == VKL_SOURCE_ORIGIN_NONE)
        {
            log_error(
                "source type %d #%d is not set, skip visual update", source->source_type,
                source->pipeline_idx);

            // NOTE: mark the binding corresponding to the source's pipeline as invalid.
            visual->bindings[source->pipeline_idx].obj.status = VKL_OBJECT_STATUS_INVALID;
            for (uint32_t j = 0; j < source->other_count; j++)
            {
                visual->bindings[source->other_idxs[j]].obj.status = VKL_OBJECT_STATUS_INVALID;
            }

            break;
        }

        // Upload only for sources manages by visky.
        to_upload =
            source->origin == VKL_SOURCE_ORIGIN_LIB || source->origin == VKL_SOURCE_ORIGIN_NOBAKE;
        if (!to_upload)
        {
            log_trace(
                "skip data upload for source type %d #%d, origin %d, that is handled by user", //
                source->source_type, source->pipeline_idx, source->origin);
            continue;
        }

        if (source->obj.status == VKL_OBJECT_STATUS_INIT)
        {
            log_error(
                "data source %d #%d was never set", source->source_type, source->pipeline_idx);
            continue;
        }
        else if (source->obj.status != VKL_OBJECT_STATUS_NEED_UPDATE)
        {
            log_trace("skip data upload for source that doesn't need to be updated");
            continue;
        }

        arr = &source->arr;

        // Update buffer sources.
        if (_source_is_buffer(source->source_kind))
        {
            br = &source->u.br;

            // NOTE: the source array MUST have been allocated by the baking function,
            // or directly by the user via vkl_visual_data_buffer() (NOBAKE origin)
            ASSERT(arr->item_count > 0);
            ASSERT(arr->item_size > 0);

            // Make sure the GPU buffer exists and is allocated with the right size.
            vkl_visual_buffer_alloc(visual, source);

            ASSERT(br->size > 0);
            ASSERT(br->buffer != VK_NULL_HANDLE);

            log_trace(
                "upload buffer for automatically-handled source %d #%d", //
                source->source_type, source->pipeline_idx);

            if (_uniform_source_is_immediate(source))
                vkl_upload_buffers_immediate(canvas, *br, true, 0, br->size, arr->data);
            else
                vkl_upload_buffers(ctx, *br, 0, br->size, arr->data);
            source->obj.status = VKL_OBJECT_STATUS_CREATED;
            visual->obj.status = VKL_OBJECT_STATUS_CREATED;
        }

        // Update textures.
        else if (_source_is_texture(source->source_kind))
        {
            // Make sure the GPU texture exists and is allocated with the right shape.
            vkl_visual_texture_alloc(visual, source);
            texture = source->u.tex;

            ASSERT(texture != NULL);
            ASSERT(is_obj_created(&texture->obj));
            // NOTE: the source array MUST have been allocated by the baking function
            ASSERT(arr->item_count > 0);
            ASSERT(arr->item_size > 0);
            ASSERT(arr->ndims >= 1);
            ASSERT(arr->shape[0] > 0);
            ASSERT(arr->shape[1] > 0);
            ASSERT(arr->shape[2] > 0);

            log_debug(
                "upload texture for automatically-handled source %d #%d, shape %dx%dx%d", //
                source->source_type, source->pipeline_idx,                                //
                arr->shape[0], arr->shape[1], arr->shape[2]);
            vkl_upload_texture(ctx, texture, arr->item_count * arr->item_size, arr->data);
            source->obj.status = VKL_OBJECT_STATUS_CREATED;
            visual->obj.status = VKL_OBJECT_STATUS_CREATED;
        }
    }

    // Update the bindings that need to be updated.
    for (uint32_t i = 0; i < visual->graphics_count; i++)
        if (visual->bindings[i].obj.status == VKL_OBJECT_STATUS_NEED_UPDATE)
            vkl_bindings_update(&visual->bindings[i]);
    for (uint32_t i = 0; i < visual->compute_count; i++)
        if (visual->bindings_comp[i].obj.status == VKL_OBJECT_STATUS_NEED_UPDATE)
            vkl_bindings_update(&visual->bindings_comp[i]);
}
