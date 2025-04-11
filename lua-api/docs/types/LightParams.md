---
layout: default
title: LightParams
parent: Lua API
permalink: lua-api/types/LightParams
---

{% raw %}

# class LightParams





Parameters for lighting

[<a href="https://github.com/saurtron/spring/blob/625902d539f43871ceb4ecdc45fc539e91a71b55/rts/Lua/LuaUnsyncedCtrl.cpp#L1514-L1540" target="_blank">source</a>]





## fields


### LightParams.position

```lua
LightParams.position : { py: number,pz: number,px: number }
```




### LightParams.direction

```lua
LightParams.direction : { dx: number,dy: number,dz: number }
```




### LightParams.ambientColor

```lua
LightParams.ambientColor : { green: number,red: number,blue: number }
```




### LightParams.diffuseColor

```lua
LightParams.diffuseColor : { red: number,green: number,blue: number }
```




### LightParams.specularColor

```lua
LightParams.specularColor : { red: number,blue: number,green: number }
```




### LightParams.intensityWeight

```lua
LightParams.intensityWeight : { specularWeight: number,diffuseWeight: number,ambientWeight: number }
```




### LightParams.ambientDecayRate

```lua
LightParams.ambientDecayRate : { ambientRedDecay: number,ambientGreenDecay: number,ambientBlueDecay: number }
```



Per-frame decay of `ambientColor` (spread over TTL frames)


### LightParams.diffuseDecayRate

```lua
LightParams.diffuseDecayRate : { diffuseRedDecay: number,diffuseBlueDecay: number,diffuseGreenDecay: number }
```



Per-frame decay of `diffuseColor` (spread over TTL frames)


### LightParams.specularDecayRate

```lua
LightParams.specularDecayRate : { specularGreenDecay: number,specularRedDecay: number,specularBlueDecay: number }
```



Per-frame decay of `specularColor` (spread over TTL frames)


### LightParams.decayFunctionType

```lua
LightParams.decayFunctionType : { diffuseDecayType: number,specularDecayType: number,ambientDecayType: number }
```



If value is `0.0` then the `*DecayRate` values will be interpreted as linear, otherwise exponential.


### LightParams.radius

```lua
LightParams.radius : number
```




### LightParams.fov

```lua
LightParams.fov : number
```




### LightParams.ttl

```lua
LightParams.ttl : number
```




### LightParams.priority

```lua
LightParams.priority : number
```




### LightParams.ignoreLOS

```lua
LightParams.ignoreLOS : boolean
```






{% endraw %}