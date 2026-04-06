#include <assert.h>

#include "render/fx_renderer.h"

struct fx_render_pass *fx_renderer_init_render_pass(struct fx_renderer *fx_renderer,
		struct wlr_render_pass *wlr_render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output) {
	if (fx_renderer == NULL || fx_renderer->wlr_renderer != output->renderer) {
		return NULL;
	}
	return fx_renderer->impl->render_pass_allocate(fx_renderer, wlr_render_pass, wlr_buffer, output);
}

void fx_render_pass_init(struct fx_render_pass *render_pass,
		const struct fx_render_pass_impl *impl, struct fx_renderer *fx_renderer,
		struct wlr_render_pass *wlr_render_pass, struct wlr_buffer *wlr_buffer,
		struct wlr_output *output) {
	assert(impl->destroy);
	*render_pass = (struct fx_render_pass) {
		.fx_renderer = fx_renderer,
		.render_pass = wlr_render_pass,
		.has_blur = false,
		.impl = impl,
	};

	pixman_region32_init(&render_pass->blur_padding_region);
}

void fx_render_pass_destroy(struct fx_render_pass *render_pass) {
	render_pass->impl->destroy(render_pass);
}

void fx_render_pass_add_texture(struct fx_render_pass *render_pass,
		const struct fx_render_texture_options *fx_options) {
	if (render_pass->impl->add_texture != NULL) {
		render_pass->impl->add_texture(render_pass, fx_options);
	}
}

void fx_render_pass_add_rect(struct fx_render_pass *render_pass,
		const struct fx_render_rect_options *fx_options) {
	if (render_pass->impl->add_rect != NULL) {
		render_pass->impl->add_rect(render_pass, fx_options);
	}
}

void fx_render_pass_add_rect_grad(struct fx_render_pass *render_pass,
		const struct fx_render_rect_grad_options *fx_options) {
	if (render_pass->impl->add_rect_grad != NULL) {
		render_pass->impl->add_rect_grad(render_pass, fx_options);
	}
}

void fx_render_pass_add_rounded_rect(struct fx_render_pass *render_pass,
		const struct fx_render_rounded_rect_options *fx_options) {
	if (render_pass->impl->add_rounded_rect != NULL) {
		render_pass->impl->add_rounded_rect(render_pass, fx_options);
	}
}

void fx_render_pass_add_rounded_rect_grad(struct fx_render_pass *render_pass,
		const struct fx_render_rounded_rect_grad_options *fx_options) {
	if (render_pass->impl->add_rounded_rect_grad != NULL) {
		render_pass->impl->add_rounded_rect_grad(render_pass, fx_options);
	}
}

void fx_render_pass_add_box_shadow(struct fx_render_pass *render_pass,
		const struct fx_render_box_shadow_options *fx_options) {
	if (render_pass->impl->add_box_shadow != NULL) {
		render_pass->impl->add_box_shadow(render_pass, fx_options);
	}
}

void fx_render_pass_add_blur(struct fx_render_pass *render_pass,
		struct fx_render_blur_pass_options *fx_options) {
	if (render_pass->impl->add_blur != NULL) {
		render_pass->impl->add_blur(render_pass, fx_options);
	}
}

bool fx_render_pass_add_optimized_blur(struct fx_render_pass *render_pass,
		struct fx_render_blur_pass_options *fx_options) {
	if (render_pass->impl->add_optimized_blur != NULL) {
		return render_pass->impl->add_optimized_blur(render_pass, fx_options);
	}
	return false;
}

void fx_render_pass_read_to_buffer(struct fx_render_pass *render_pass,
		pixman_region32_t *region, struct wlr_buffer *dst_buffer,
		struct wlr_buffer *src_buffer) {
	if (render_pass->impl->read_to_buffer != NULL) {
		render_pass->impl->read_to_buffer(render_pass, region, dst_buffer, src_buffer);
	}
}

void fx_render_pass_save_blur_region(struct fx_render_pass *render_pass,
		pixman_region32_t *region) {
	if (render_pass->impl->save_blur_region != NULL) {
		render_pass->impl->save_blur_region(render_pass, region);
	}
}


void fx_render_pass_apply_saved_blur_region(struct fx_render_pass *render_pass,
		pixman_region32_t *region) {
	if (render_pass->impl->apply_saved_blur_region != NULL) {
		render_pass->impl->apply_saved_blur_region(render_pass, region);
	}
}
