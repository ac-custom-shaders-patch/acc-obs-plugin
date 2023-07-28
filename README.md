# Assetto Corsa OBS Helper

<a href="https://files.acstuff.ru/shared/R72y/demo.mp4" target="_blank"><img src="https://files.acstuff.ru/shared/4DXZ/screen.jpg" width="380"></a>

## Introduction

A plugin for OBS Studio providing direct access to various Assetto Corsa textures. Might help if you need to record some gameplay without HUD, or compose a recording or a stream with extra widgets or extra camera angles. Requires CSP v0.1.80-preview348 (build 2503) or newer. Don’t forget to enable OBS integration option in Small Tweaks module as well.

## Installation

To install plugin, [download the latest version](/releases/latest/download/acc-obs-plugin.zip), extract the DLL file to “obs-plugins\64bit” folder of your OBS Studio installation and restart the OBS Studio. Also, make sure you have OBS Integration in Small Tweaks settings enabled.

## New textures

All textures are rendered only if selected in OBS Studio and currently visible. You can also optionally lower refresh rate, that could also help with performance. 

- Default
    - Clean view: final AC scene without HUD.
    - Include HUD: pretty much the same image as the one captured by OBS Studio by default.
    - Virtual mirror: texture used for virtual mirror (if you have Real Mirrors option enabled without virtual mirror support active, Real Mirrors will pause while this texture is used).
- Extra:
    - VR HUD: semi-transparent texture containing HUD image for 3D VR HUD.
    - Redirected apps: separate buffer to which you can redirect some of the AC, Python or Lua apps (tool for configuring appears once the texture is selected, [demonstration](https://files.acstuff.ru/shared/R72y/demo.mp4)). With it you can, for example, install some third-party widgets to only appear in the video.
    - Android Auto: if your car has Android Auto added, it’ll be mirrored in that texture.
- From outside:
    - Above: shows car from above, might be helpful if to monitor contacts.
    - Chase camera: very basic third-person camera.
    - Track camera: show selected car from spectator position.

Third-party Lua apps can add their own textures, scroll a bit further for more information.

## Known issues

- For direct texture forwarding to work, make sure OBS Studio uses the same GPU as does Assetto Corsa.
- Only 32-bit OBS Studio for Windows is currently supported.

# Custom Textures

Library `utils/obs` in “extension\internal\lua-shared” can help with adding custom texture sources to your Lua apps. Make sure to check out the actual file’s contents for more details about optimal usage. Some examples:

```lua
-- Access OBS helper library:
local obs = require('shared/utils/obs')

-- Simply redirect an existing texture to OBS:
obs.register(obs.Groups.Extra, 'Scene depth', obs.Flags.Monochrome, nil, 'dynamic::depth')
obs.register(obs.Groups.Extra, 'Scene HDR', obs.Flags.Opaque, nil, 'dynamic::hdr')

-- Redirect a GIF (would also work with a video player):
local gif = ui.GIFPlayer('animated.gif')
obs.register(obs.Groups.Extra, 'Animated GIF', obs.Flags.Transparent, nil, gif)

-- Simple proceducal examples:
obs.register(obs.Groups.Extra, 'Flashing color', obs.Flags.Transparent, vec2(256, 256), function (canvas)
    canvas:clear(table.random(rgbm.colors))
end)
obs.register(obs.Groups.Extra, 'Transparent', obs.Flags.Transparent, vec2(512, 128), function (canvas)
  canvas:update(function (dt)
    ui.drawText(math.random(0, 10), vec2(math.random() * 500, math.random() * 120), table.random(rgbm.colors))
  end)
end)

-- More complicated example rendering a scene with a size configurable in OBS Studio:
local spectatorShot
local spectator = obs.register('Third-party app', 'Spectator view', obs.Flags.ManualUpdate + obs.Flags.ApplyCMAA + obs.Flags.UserSize, function (size) 
  if spectatorShot then spectatorShot:dispose() end
  spectatorShot = ac.GeometryShot(ac.findNodes('sceneRoot:yes'), size, 1, false, render.AntialiasingMode.None, render.TextureFormat.R11G11B10.Float)
  spectatorShot:setClippingPlanes(0.5, 5e3)
  spectatorShot:setBestSceneShotQuality()
end, function (canvas)
  spectatorShot:updateWithTrackCamera(0)
  canvas:updateWithShader({ 
    -- Converting HDR to LDR:
    textures = { txHDR = spectatorShot },
    shader = [[float4 main(PS_IN pin){ 
      float4 r = txHDR.Sample(samLinear, pin.Tex); 
      r = r / (1 + r); 
      return float4(r.rgb, 1); 
    }]]
  })
end)

function script.simUpdate()
  -- Updating a shot in [SIM_CALLBACKS] UPDATE callback is the optimal way to 
  -- make sure everything is up-to-time
  spectator:update()
end
```

# Licence

This plugin is licensed under GPLv2 only due to being linked with OBS Studio licensed under GPLv2, otherwise it would be the good old MIT license. This, of course, does not apply to Lua apps adding custom textures: those can be of any license.
