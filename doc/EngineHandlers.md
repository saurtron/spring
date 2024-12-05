## Engine Gadgets

In this document, I propose a system of **Engine Gadgets**.

**note:** the name is just an idea, maybe a less confusing name can be found, like recoilets I dont know XD.

Such gadgets could provide:

- More modularity.
- A place for custom behaviours that don't need to modify other files.
- More extensibility and organization to the lua api.
- An easy way for games to convert some of their gadgets/widgets to c++ and share with other projects.

Check [here](/rts/Custom/) for a proof of concept implementation, migrating some easy but heavy bar widgets/gadgets.

### Motivation

Currently there is no clear mechanism to implement activable engine modules.

These modules could implement useful functionality not all games would like to use, or where a game might later want to override by disabling it.

#### Performance

While lua is a great language and it's really fast, it also has its limitations.

Games are the moment using lots of gadgets and widgets, for example bar at the moment has around 250 gadgets and 250 widgets in use.

Most of them are situational and don't run very often, being quite light on performance.

Some other mechanisms, though, are difficult to optimize.

For example the main callbacks running when giving commands, AllowCommand and UnitCommand, can quickly degenerate into 1000s of lua calls.

An example scenario, with 300 units attacking another 200, can result in `300*200*(AllowCommand listeners + UnitCommand listeners)`. This is 60k calls, that will result in 60k to 180k calls into lua (due to the widget/synced/unsynced structure).

Now think, one microsecond already becomes 100ms when spent so many times. Too much even for 30fps. And that's not even an incredibly big command.

By providing a path for optimizing, maybe in the future more and more restrictions can be lifted, and bigger and bigger games can be possible. Currently the late game is many times unpracticable for many users, where they start to lag or experience bad framerates.

#### Engine extensibility

The engine at the moment, while having a great extensibility mechanism with EventClient interface, doesn't have a way to enable and disable clients from lua.

Also, such modules don't have a mechanism for exporting lua methods, relying currently on always adding functions in a number of lua files, with some of them reaching 5000+ lines.

With a bit of work, such gadgets could export their EventClient interface, for further controlability from lua. Also later they could even declare some of their other methods for exporting directly by providing some convenience mechanism so they don't need to write lua code directly.

#### Extending the EventClient

It could be argued we have the EventClient interface now, pretty much such a system already in place, why extend this?

In my opinion, as seen by the proof of concept implementation, extending the EventClient with a layer to allow further control can allow great power, while not requiring changes or disturbances on the rest of the engine, if at all.

### C++ api

The C++ api could be based on a gadgetHandler and a CGadget base object. This can make gadgets clean and easy to control.

The api is mostly internal, and games are expected to control this from lua.

CGadget makers just need to clone the [example gadgets](/rts/Custom/).

#### gadgetHandler

```
  bool IsGadgetEnabled(string name);
  bool EnableGadget(string name, bool enable, int priority=0);

  void EnableAll(bool enable);
  void AddFactory(CGadgetFactory* fact);

```

#### CGadget

```
  bool IsEnabled()
  void Enable();
  void Disable();
```


### Lua api

These methods would allow games to enable desired modules from synced code.

Also they would allow querying for enabled modules, so other lua method can react to them being available or not.

```
  Spring.EnableEngineGadget(name, enable, priority)
  Spring.IsEngineGadgetEnabled(name)
```

later on could be added:

```
  Spring.GetEngineGadget(name)
```

This could also be exported into a different module, like EngineGadgets:


```
  -- some ideas:
  EngineGadgets.IsGadgetEnabled(name)
  EngineGadgets.EnabledGadget(name)
  EngineGadgets.<gadgetname>.<method>:
  EngineGadgets.UnitImmobileBuilder.IsEnabled()
  EngineGadgets.UnitImmobileBuilder.Enable()
  EngineGadgets.UnitImmobileBuilder.Disable()
  EngineGadgets.UnitImmobileBuilder.AllowCommand(...)
  EngineGadgets.UnitImmobileBuilder.CustomMethod(...)
  gadget = EngineGadgets.UnitImmobileBuilder
  gadget = EngineGadgets.Get("UnitImmobileBuilder")
  gadget.IsEnabled()
  gadget.Enable()
  gadget.AllowCommand(...)
  ...
```

### Links

Reference lua widgets:

- [nano_range_checker](https://github.com/beyond-all-reason/Beyond-All-Reason/pull/3908/files) ([c++](https://github.com/saurtron/spring/blob/engine-gadgets/rts/Custom/BuilderRangeCheck.cpp))
- [cmd_guard_remove](https://github.com/beyond-all-reason/Beyond-All-Reason/blob/master/luaui/Widgets/cmd_guard_remove.lua) ([c++](https://github.com/saurtron/spring/blob/engine-gadgets/rts/Custom/GuardRemove.cpp))
- [unit_immobile_builder](https://github.com/beyond-all-reason/Beyond-All-Reason/blob/eb6b3cf60d5e64bf09773ec4a452fa7fd978a794/luaui/Widgets/unit_immobile_builder.lua) ([c++](https://github.com/saurtron/spring/blob/engine-gadgets/rts/Custom/UnitImmobileBuilder.cpp))

Simple widgets that have a high cost due to command queue and AllowCommand/UnitCommand usage.
