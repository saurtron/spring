## Engine Gadgets

Currently there is no clear mechanism to implement activable engine modules.

These modules could implement useful functionality that not all games would like to implement, or where a game might later want to override it by disabling it.

So, in this document, I propose a system of **Engine Gadgets**.

**note:** the name is just an idea, maybe a less confusing name can be found, like recoilets I dont know XD.

Such gadgets could provide:

- More modularity.
- A place for custom behaviours that don't need to modify other files.
- More extensibility and organization to the lua api.
- An easy way for games to convert some of their gadgets/widgets to c++ and share with other projects.

### Motivation

#### Performance

While lua is a great language and it's really fast, it also has its limitations.

Games are the moment using lots of gadgets and widgets, for example bar at the moment has around 250 gadgets and 250 widgets in use.

Most of them are situational and don't run very often, being quite light on performance.

Some other mechanisms, though, are difficult to optimize.

For example the main callbacks running when giving commands, AllowCommand and UnitCommand, can quickly degenerate into 1000s of lua calls.

An example scenario, with 300 units attacking another 200, can result in 300*200*(AllowCommand listeners + UnitCommand listeners). This is 60k calls, that will result in 60k to 180k calls into lua (due to the widget/synced/unsynced structure).

Now think, one microsecond already becomes 100ms when spent so many times. Too much for one frame. And that's not even an incredibly big command.

#### Engine extensibility

The engine at the moment, while having a great extensibility mechanism with EventClient interface, doesn't have a way to enable and disable clients from lua.

Also such modules don't have a mechanism for exporting lua methods, relying currently on always adding functions in a number of lua files, with some of them reaching 5000+ lines.

With a bit of work, such gadgets could export their EventClient interface, for further controlability form lua. Also later they could even declare some of their other methods for exporting directly.


### C++ api

The C++ api could be based on a gadgetHandler and a CGadget base object. This can make gadgets clean and easy to control.

The api is mostly internal, and games are expected to control this from lua.

CGadget makers just need to clone the example gadgets.

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
