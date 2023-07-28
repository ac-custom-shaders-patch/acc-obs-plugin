#pragma warning(push)
#pragma warning(disable : 4005)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NODRAWTEXT
#define NOMCX
#define NOSERVICE
#define NOHELP
#pragma warning(pop)

#include <obs-module.h>
#include <stdio.h>
#include <Windows.h>

#define MAX_TEXTURES 63
#define NAME_LENGTH 48
#define DESCRIPTION_LENGTH 256

#define FLAG_TEXTURE_UNAVAILABLE (1 << 0)
#define FLAG_TEXTURE_TRANSPARENT (1 << 1)
#define FLAG_TEXTURE_SRGB (1 << 2)
#define FLAG_TEXTURE_MONOCHROME (1 << 3)
#define FLAG_TEXTURE_HDR (1 << 4)
#define FLAG_TEXTURE_USER_SIZE (1 << 7)
#define FLAG_TEXTURE_OVERRIDE_SIZE (1 << 8)

struct accsp_source
{
	uint32_t handle;
	uint32_t name_key;
	uint16_t width;
	uint16_t height;
	uint16_t needs_data;
	uint16_t flags;
	char name[NAME_LENGTH];
	char description[DESCRIPTION_LENGTH];
};

struct accsp_data
{
	uint32_t alive_counter;
	int32_t items_count;
	struct accsp_source items[MAX_TEXTURES];
};

static HANDLE accsp_handle;
static LPVOID accsp_view;

static struct accsp_data* try_access_data(void)
{
	if (!accsp_handle)
	{
		accsp_handle = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE,
			L"Local\\AcTools.CSP.OBSTextures.v0");
	}
	if (accsp_handle && !accsp_view)
	{
		accsp_view = MapViewOfFile(accsp_handle, FILE_MAP_READ | FILE_MAP_WRITE,
			0, 0, sizeof(struct accsp_data));
	}
	return accsp_view;
}

struct accsp_texture
{
	gs_texture_t* texture;
	uint16_t skip_frames;
	uint16_t skipping;
	uint32_t texture_handle;
	uint32_t last_name_key;
	uint32_t last_index;
	uint16_t width;
	uint16_t height;
	uint16_t user_width;
	uint16_t user_height;
	float multiplier;
	char name[NAME_LENGTH];
};

static void accsp_texture_sync_props(struct accsp_texture* ctx, struct accsp_source* known)
{
	if ((known->flags & FLAG_TEXTURE_USER_SIZE) != 0
		&& (known->width != ctx->user_width || known->height != ctx->user_height))
	{
		known->width = ctx->user_width;
		known->height = ctx->user_height;
		known->flags |= FLAG_TEXTURE_OVERRIDE_SIZE;
	}
	ctx->width = known->width;
	ctx->height = known->height;
}

static struct accsp_source* accsp_texture_sync(struct accsp_texture* ctx)
{
	struct accsp_data* shared = try_access_data();
	if (shared && shared->items_count >= 0)
	{
		shared->alive_counter = 60;
		struct accsp_source* known = &shared->items[ctx->last_index];
		if (known->name_key == ctx->last_name_key)
		{
			accsp_texture_sync_props(ctx, known);
			return known;
		}
		for (int32_t i = 0; i < shared->items_count; ++i)
		{
			if (strncmp(shared->items[i].name, ctx->name, NAME_LENGTH) != 0) continue;			
			accsp_texture_sync_props(ctx, &shared->items[i]);
			ctx->last_name_key = shared->items[i].name_key;
			ctx->last_index = i;
			return &shared->items[i];
		}
	}
	return NULL;
}

static void accsp_texture_update(void* data, obs_data_t* settings)
{
	struct accsp_texture* ctx = data;
	ctx->multiplier = (float)(obs_data_get_int(settings, "brightness") / 100.);
	ctx->skip_frames = (uint16_t)obs_data_get_int(settings, "skip");
	ctx->user_width = (uint16_t)obs_data_get_int(settings, "width");
	ctx->user_height = (uint16_t)obs_data_get_int(settings, "height");
	ctx->skipping = UINT16_MAX;
	const char* name = obs_data_get_string(settings, "name");
	if (strcmp(ctx->name, name) != 0)
	{
		strcpy(ctx->name, name);
		ctx->last_name_key = 0;
	}
	accsp_texture_sync(ctx);
}

static void* accsp_texture_create(obs_data_t* settings, obs_source_t* _)
{
	struct accsp_texture* ctx = bzalloc(sizeof(struct accsp_texture));
	accsp_texture_update(ctx, settings);
	return ctx;
}

static void accsp_texture_destroy(void* data)
{
	if (data)
	{
		struct accsp_texture* ctx = data;
		if (ctx->texture)
		{
			obs_enter_graphics();
			gs_texture_destroy(ctx->texture);
			obs_leave_graphics();
		}
	}
	bfree(data);
}

static void accsp_video_tick(void* data, float _)
{
	struct accsp_texture* ctx = data;
	struct accsp_source* found = accsp_texture_sync(ctx);
	if (found == NULL || (found->flags & FLAG_TEXTURE_UNAVAILABLE) != 0) return;

	found->needs_data = ctx->skip_frames == 0 || ctx->skipping == UINT16_MAX ? 3 : ctx->skipping == 0 ? 1 : 0;
	ctx->skipping = ctx->skipping == UINT16_MAX ? 0 : ctx->skipping ? ctx->skipping - 1 : ctx->skip_frames;
}

static void accsp_texture_render(void* data, gs_effect_t* _)
{
	struct accsp_texture* ctx = data;
	struct accsp_source* found = accsp_texture_sync(ctx);
	if (found == NULL || (found->flags & FLAG_TEXTURE_UNAVAILABLE) != 0) return;

	if (found->handle != ctx->texture_handle)
	{
		ctx->texture_handle = found->handle;
		gs_texture_destroy(ctx->texture);
		ctx->texture = found->handle ? gs_texture_open_shared(found->handle) : NULL;
	}

	if (!ctx->texture) return;
	const bool allow_transparency = (found->flags & FLAG_TEXTURE_TRANSPARENT) != 0;
	const bool texture_srgb = (found->flags & FLAG_TEXTURE_SRGB) != 0;
	gs_effect_t* const effect = obs_get_base_effect(allow_transparency ? OBS_EFFECT_DEFAULT : OBS_EFFECT_OPAQUE);
	gs_eparam_t* const image = gs_effect_get_param_by_name(effect, "image");
	const bool previous = gs_framebuffer_srgb_enabled();
	while (gs_effect_loop(effect, "DrawMultiply"))
	{
		gs_enable_framebuffer_srgb(false);
		gs_enable_blending(allow_transparency);
		(texture_srgb ? gs_effect_set_texture_srgb : gs_effect_set_texture)(image, ctx->texture);
		gs_effect_set_float(gs_effect_get_param_by_name(effect, "multiplier"), ctx->multiplier);
		gs_draw_sprite(ctx->texture, 0, 0, 0);
		gs_enable_blending(true);
		gs_enable_framebuffer_srgb(previous);
	}
}

static void fill_remaining_properties(obs_properties_t* props, struct accsp_texture* ctx)
{
	struct accsp_source* found = accsp_texture_sync(ctx);
	obs_property_t* p;
	if (found && found->description[0] != 0)
	{
		p = obs_properties_add_text(props, "@description", found->description, OBS_TEXT_INFO);
		obs_property_text_set_info_type(p, OBS_TEXT_INFO_NORMAL);
		obs_property_text_set_info_word_wrap(p, true);
	}
	if (found && (found->flags & FLAG_TEXTURE_USER_SIZE) != 0)
	{
		p = obs_properties_add_int(props, "width", "Width", 32, 2048, 1);
		obs_property_set_long_description(p, "Configured size is shared across all instances of the same type.");
		p = obs_properties_add_int(props, "height", "Height", 32, 2048, 1);
		obs_property_set_long_description(p, "Configured size is shared across all instances of the same type.");
	}

	p = obs_properties_add_int_slider(props, "brightness", "Brightness", 0, 300, 1);
	obs_property_int_set_suffix(p, "%");
	obs_property_set_long_description(p, "Simple LDR brightness multiplier");

	p = obs_properties_add_int_slider(props, "skip", "Skip frames", 0, 12, 1);
	obs_property_int_set_suffix(p, " frame(s)");
	obs_property_set_long_description(p, "Skipping a frame or two might help with performance as well");
}

static bool accsp_refresh_needed(void* priv, obs_properties_t* props, obs_property_t* prop, obs_data_t* settings)
{
	struct accsp_texture* ctx = priv;
	const char* name = obs_data_get_string(settings, "name");
	if (strcmp(ctx->name, name) != 0)
	{
		strcpy(ctx->name, name);
		ctx->last_name_key = 0;
	}	
	obs_properties_remove_by_name(props, "@description");
	obs_properties_remove_by_name(props, "width");
	obs_properties_remove_by_name(props, "height");
	obs_properties_remove_by_name(props, "brightness");
	obs_properties_remove_by_name(props, "skip");
	fill_remaining_properties(props, ctx);
	return true;
}

static obs_properties_t* accsp_texture_properties(void* data)
{
	obs_properties_t* props = obs_properties_create();
	obs_property_t* p;
	struct accsp_data* shared = try_access_data();
	if (!shared)
	{
		p = obs_properties_add_text(props, "@warning",
			"Failed to connect to Assetto Corsa. Make sure the game is running and OBS integration in Small Tweaks is enabled.", OBS_TEXT_INFO);
		obs_property_text_set_info_type(p, OBS_TEXT_INFO_WARNING);
		obs_property_text_set_info_word_wrap(p, true);
	}
	else if (shared->items_count == -1)
	{
		p = obs_properties_add_text(props, "@warning",
			"Assetto Corsa has stopped (or its Small Tweaks Lua module collapsed). Consider restarting the session.", OBS_TEXT_INFO);
		obs_property_text_set_info_type(p, OBS_TEXT_INFO_WARNING);
		obs_property_text_set_info_word_wrap(p, true);
	}
	else if (shared->items_count == 0)
	{
		p = obs_properties_add_text(props, "@warning",
			"No available textures. Something might have been broken.", OBS_TEXT_INFO);
		obs_property_text_set_info_type(p, OBS_TEXT_INFO_WARNING);
		obs_property_text_set_info_word_wrap(p, true);
	}
	else
	{
		struct accsp_texture* ctx = data;
		p = obs_properties_add_list(props, "name", "Texture", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(p, accsp_refresh_needed, ctx);
		for (int32_t i = 0; i < shared->items_count; ++i)
		{
			const char* flag = NULL;
			if (shared->items[i].flags & FLAG_TEXTURE_UNAVAILABLE) flag = "unavailable";
			else if (shared->items[i].flags & FLAG_TEXTURE_HDR) flag = "HDR";
			else if (shared->items[i].flags & FLAG_TEXTURE_MONOCHROME) flag = "monochrome";
			else if (shared->items[i].flags & FLAG_TEXTURE_TRANSPARENT) flag = "transparent";
			else if (shared->items[i].flags & FLAG_TEXTURE_USER_SIZE) flag = "resizeable";
			char name_fmt[256];
			obs_property_list_add_string(p, flag && sprintf_s(name_fmt, 256, "%s (%s)", shared->items[i].name, flag) != -1 
				? name_fmt : shared->items[i].name, shared->items[i].name);
		}		
		fill_remaining_properties(props, ctx);
	}
	return props;
}

static const char* accsp_texture_get_name(void* _) { return "Assetto Corsa"; }
static uint32_t accsp_texture_getwidth(void* data) { return ((struct accsp_texture*)data)->width; }
static uint32_t accsp_texture_getheight(void* data) { return ((struct accsp_texture*)data)->height; }

static void accsp_texture_defaults(obs_data_t* settings)
{
	obs_data_set_default_string(settings, "name", "Scene");
	obs_data_set_default_int(settings, "brightness", 100);
	obs_data_set_default_int(settings, "skip", 0);
	obs_data_set_default_int(settings, "width", 640);
	obs_data_set_default_int(settings, "height", 640);
}

struct obs_source_info accsp_texture_info = {
	.id = "accsp_capture",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.create = accsp_texture_create,
	.destroy = accsp_texture_destroy,
	.update = accsp_texture_update,
	.get_name = accsp_texture_get_name,
	.get_defaults = accsp_texture_defaults,
	.get_width = accsp_texture_getwidth,
	.get_height = accsp_texture_getheight,
	.video_tick = accsp_video_tick,
	.video_render = accsp_texture_render,
	.get_properties = accsp_texture_properties,
	.icon_type = OBS_ICON_TYPE_GAME_CAPTURE,
};

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("x4fab")

MODULE_EXPORT const char* obs_module_name(void) { return "Assetto Corsa integration"; }
MODULE_EXPORT const char* obs_module_description(void) { return "Windows game/Assetto Corsa integration"; }

bool obs_module_load(void)
{
	obs_register_source(&accsp_texture_info);
	return true;
}
