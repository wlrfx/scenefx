embed = find_program('./embed.sh', native: true)

shaders = [
    'common.vert',
	'gradient.frag',
    'corner_alpha.frag',
    'quad.frag',
    'quad_grad.frag',
    'quad_round.frag',
    'quad_grad_round.frag',
    'tex.frag',
    'box_shadow.frag',
    'blur1.frag',
    'blur2.frag',
    'blur_effects.frag',
]

foreach name : shaders
    output = name.underscorify() + '_gles3_src.h'
    var = name.underscorify() + '_gles3_src'
    scenefx_files += custom_target(
        output,
        command: [embed, var],
        input: name,
        output: output,
        feed: true,
        capture: true,
    )
endforeach

