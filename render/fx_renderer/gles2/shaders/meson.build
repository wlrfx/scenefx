embed = find_program('./embed.sh', native: true)

shaders = [
    'common.vert',
    'quad.frag',
    'quad_round.frag',
    'tex.frag',
]

foreach name : shaders
    output = name.underscorify() + '_src.h'
    var = name.underscorify() + '_src'
    wlr_files += custom_target(
        output,
        command: [embed, var],
        input: name,
        output: output,
        feed: true,
        capture: true,
    )
endforeach
