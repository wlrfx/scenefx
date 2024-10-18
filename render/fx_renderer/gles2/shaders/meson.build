embed = find_program('./embed.sh', native: true)

shaders = [
    'common.vert',
    'quad.frag',
    'quad_grad.frag',
    'quad_round.frag',
    'quad_grad_round.frag',
    'tex.frag',
    'rounded_border_corner.frag',
    'rounded_grad_border_corner.frag',
    'box_shadow.frag',
    'blur1.frag',
    'blur2.frag',
    'blur_effects.frag',
]

foreach name : shaders
    output = name.underscorify() + '_src.h'
    var = name.underscorify() + '_src'
    scenefx_files += custom_target(
        output,
        command: [embed, var],
        input: name,
        output: output,
        feed: true,
        capture: true,
    )
endforeach

