embed = find_program('./embed.sh', native: true)

shaders = [
    'common.vert',
    'quad.frag',
    'quad_round.frag',
    'tex.frag',
    'border.frag',
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

