# Zombie Master: Reborn — Feature Changelog

This document summarizes all features and improvements added on top of the official ZMR GitHub source.

---

## AI Systems

### AI Zombie Master (`zm_sv_ai_zm`)

The server can run a fully automated Zombie Master. Four modes are available:

| Mode | Value | Description |
|------|-------|-------------|
| **Disabled** | `0` | No AI ZM |
| **Equal Chance** | `1` | AI enters the ZM draw alongside human volunteers with an equal chance |
| **Fallback** | `2` | AI takes over only when no human volunteers for ZM **(default)** |
| **Forced** | `3` | AI is always ZM — no human can be picked. Spectators can still take over by pressing USE on the AI ZM bot |

**Persistence:** The AI ZM persists across rounds automatically. When a human player takes over (via spectator USE or volunteer), the AI ZM bot is kicked and a fresh survivor bot fills the slot.

**Phase cycle:** The AI uses a 3-phase cycle — **Spawn → Hidden Spawn → Reserve → Spawn → ...** — starting with zombies from the very first tick:

1. **Spawn** (start of round) — Pick a burst of 1–15 zombies. Each zombie in the burst is independently picked using weighted type selection (60% shambler, 10% each special), so a single wave can contain a **mix of different types**. Before traps are unlocked, spends **all** resources freely. After traps are unlocked, spends only excess **above** the reserve. If a class is blocked by the spawner, at its per-type limit, or unaffordable, it is simply excluded and the remaining valid classes keep their base weights (no redistribution), preserving the intended ratio between shamblers and specials. The AI picks spawners nearest to its **focused target** (the survivor it's currently picking on) and dynamically updates the chosen spawner mid-burst as that target moves. If the AI can't afford the next zombie, it **waits** for resources to accumulate rather than skipping ahead.
2. **Hidden spawn** — Attempt 1–3 surprise spawns near random survivors. Each spawn independently picks a type using the same weighted system for mix-and-match variety. Keeps retrying different positions for up to **15 seconds** before giving up. Spends freely regardless of reserve. If Hidden Spawn All Classes is enabled, uses weighted selection across all non-limit-capped types; otherwise shamblers only. Then cycles to Reserve.
3. **Reserve** — Save resources until the AI can cover the most expensive active trap on the map. Once met, **traps are permanently unlocked** for the rest of the round and the AI immediately cycles back to Spawn. If there are no traps on the map, this phase is skipped entirely.

**Traps and explosive barrels:** Once the first reserve target is met, traps and barrels fire opportunistically in **any** phase whenever a survivor enters the configured trap range (512 units) and the AI can afford the cost. Before the reserve is met, traps never fire — the AI focuses entirely on spawning zombies first. If a trap fires mid-spawn-wave and consumes resources, the AI finishes the current phase with whatever is left, then continues normally. Explosive barrels use the same range and cooldown rules as traps.

**Focused targeting:** The AI cycles through all living survivors in **round-robin** order, focusing on each one for **5–20 seconds** at random. During that window, spawner selection prioritizes spawners nearest to that focused survivor, and the camera smoothly follows them. This ensures every survivor gets attention and challenge throughout the round.

**Camera:** The AI ZM camera uses **double-smoothed exponential interpolation** for very gentle, cinematic movement. The raw desired position is first smoothed into an intermediate target, then the actual camera smoothly glides toward that intermediate — eliminating jitter and back-and-forth whipping. It uses a fixed directional offset above the target (not based on where they're looking) so the camera stays steady even when the survivor rapidly changes facing. The camera adjusts height dynamically to avoid obstacles blocking its view of the target. On first frame it snaps to position; after that it never teleports. When the view mode is set to "Within View Only", only spawners, traps, and barrels visible from the camera position are accessible.

**Zombie culling:** When the zombie population exceeds 80% of the cap, the AI checks for stranded zombies — those with no active rally command, no enemy, no recent damage, and more than 6144 units from the nearest survivor. Stranded zombies are killed to free pop cap space. The stranded threshold is configurable. Checks run every 10 seconds.

**Rush prevention:** For a configurable number of seconds at the start of each round, the AI only moves its camera around without spawning, triggering traps, or rallying zombies.

**Rally:** The AI continuously rallies idle zombies toward the nearest survivors. When multiple survivors are close together, zombies split between targets to spread the pressure.

**Difficulty scaling:** The `zm_sv_ai_zm_difficulty` multiplier scales the AI's resource income rate.

**Debug logging:** Enable with `zm_sv_ai_zm_debug 1` in the server console (cheat-protected). Logs spawn decisions, trap triggers, barrel detonations, hidden spawns, culling events, and rally commands.

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_ai_zm` | `2` | AI ZM mode: 0=Disabled, 1=Equal Chance, 2=Fallback, 3=Forced |
| `zm_sv_ai_zm_debug` | `0` | Verbose AI ZM console logging (cheat-protected) |
| `zm_sv_ai_zm_difficulty` | `1.0` | Resource income multiplier for AI ZM (0.1–5.0) |
| `zm_sv_ai_zm_trap_range` | `512` | Survivor proximity required to trigger a trap or barrel |
| `zm_sv_ai_zm_trap_cooldown` | `30.0` | Per-entity cooldown before the AI can re-use the same trap or barrel |
| `zm_sv_ai_zm_rush_prevention` | `15.0` | Seconds at round start before the AI ZM can act |
| `zm_sv_ai_zm_view_mode` | `0` | Spawner/trap access: 0=Global, 1=Within View only |
| `zm_sv_ai_zm_rally_interval` | `6.0` | Seconds between idle zombie rally commands |
| `zm_sv_ai_zm_rally_buffer` | `256.0` | Distance buffer for splitting zombie targets between survivors |
| `zm_sv_ai_zm_cull_time` | `45.0` | Seconds a zombie must be stranded before being culled (0=disabled) |

### AI Survivor Bots (`zm_sv_bot_survivors`)

The server can automatically fill the survivor team with AI-controlled bots at round start.

**How to enable:** Check the **Auto-Fill Survivor Bots** checkbox in the Create Server menu, or set `zm_sv_bot_survivors 1` in the console. Bots fill all empty player slots up to max players. Human players automatically replace bots — bots are kicked to make room. Bots do not count toward the human player limit.

**Default behavior:** Mixed Mode — each bot is randomly assigned Follow, Explore, or Defend behavior at spawn. Configurable via `zm_sv_bot_default_behavior`.

**Bot behavior:**
- Bots follow the nearest **human** player by default, spreading out in a fan formation. Bots never follow other bots. If no human survivors are alive, bots defend the spawn area
- Press **E** while looking at a bot to toggle **Following** / **Staying**
- If you are **carrying an object** and press **E** on a bot that is carrying one, the bot drops it
- Bots search for weapons and ammo on the ground (configurable range) in **all** behavior modes — following, exploring, and defending. If a bot walks within range of a weapon type it doesn't have or ammo it needs, it detours to grab it and returns to its task. Prioritizes missing loadout slots and higher-tier weapons. Bots reassess their best weapon after every pickup (priority: primary gun → sidearm → melee → throwable → fists)
- Bots identify nearby crates by their contents (weapon, melee, throwable, or ammo) and smash open crates that contain items they need. Weapon crates are prioritized over ammo crates. Bots only target crates within 1024 units
- Bots equip the best weapon for the situation and switch to melee/fists when ranged ammo runs out
- Bots with only melee weapons prioritize fleeing to find guns rather than rushing zombies
- Bots use player-matched pathfinding, open USE-doors, jump obstacles, and break out of oscillation loops
- Bots look around naturally when idle and react to nearby sounds
- Bots shoot while moving (run-and-gun) and kite backwards from threats
- Bots crouch when attacking low objects like ammo crates
- Bots shoot explosive barrels when 5+ zombies are clustered around one
- Bots only speak voicelines for **commands** (follow, stay, defend, grab), **taunts** on kills, and **alerts** (zombie sight, explosions). No voicelines for plain USE interactions
- Dead players can possess a bot by pressing **USE** while spectating it (`zm_sv_bot_possess 1`)

**Voice command bot control:**
- **Help** (voice menu) — Nearby bots come to you. Does not grab bots already following another human player
- **Follow** (voice menu) — Bots in your **view cone** (within range, line of sight) that aren't already following someone begin following you. Shows a count of how many bots were commanded
- **Go** (voice menu) — Bots you're looking at switch to Explore mode
- **Hold E on ground** — Bots **following you** move to the aimed position and switch to Defend mode with 256-unit spacing between positions. Only affects your followers
- **Hold E on physics object** — The closest bot walks to the object and picks it up. Works on any grabbable physics object
- **Double-tap E** — All bots **following you** that are carrying objects will carry them to where you're looking (raycast) and drop them there. Single-tap E on a carrying bot drops the object in place

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_bot_survivors` | `0` | Enable auto-fill survivor bots: 0=off, 1=on |
| `zm_sv_bot_default_behavior` | `3` | Default bot mode: 0=Follow, 1=Explore, 2=Defend, 3=Mixed Mode |
| `zm_sv_bot_help_range` | `1024` | Range for Help voice command to affect bots |
| `zm_sv_bot_possess` | `1` | Allow spectators to possess bots with USE key |
| `zm_sv_bot_weapon_search_range` | `1024` | How far bots search for weapons and ammo |
| `zm_sv_bot_debug` | `0` | Enable detailed bot debug logging (cheat-protected) |

---

## Zombie Multipliers

Scale zombie stats globally for difficulty tuning.

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_zombie_health_mult` | `1.0` | Multiplier applied to all zombie max health |
| `zm_sv_zombie_damage_mult` | `1.0` | Multiplier applied to all zombie damage output |

---

## ZM Resource Scaling

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_resource_multiplier` | `1.0` | Multiplier applied to all ZM resource income |
| `zm_sv_resource_per_player_mult` | `0.0` | Extra resource fraction per survivor beyond the first (0.1 = +10% each, 1.0 = +100% each) |

---

## Infinite Flashlight

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_flashlight_infinite` | `0` | Disables flashlight battery drain when enabled |

---

## ZM Physics Explosion Damage

By default, physics explosions (barrels, etc.) do not hurt players. This can be enabled.

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_physexp_player_damage` | `0` | Max damage a physics explosion can deal to players (0 = disabled) |
| `zm_sv_physexp_ignite_barrels` | `0` | ZM blast detonates explosive barrels and breakable props in radius |

---

## Per-Zombie-Type Limits

When the **Enable Per-Zombie-Type Limits** option is checked, each special zombie type can be independently capped. Setting a type's limit to **0** blocks that type from spawning entirely (it does not mean unlimited).

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_zombie_type_limits` | `0` | Enable per-type zombie limits |
| `zm_sv_zombie_max_banshee` | `5` | Max banshees alive at once (0 = none allowed when limits enabled) |
| `zm_sv_zombie_max_hulk` | `5` | Max hulks alive at once (0 = none allowed when limits enabled) |
| `zm_sv_zombie_max_drifter` | `5` | Max drifters alive at once (0 = none allowed when limits enabled) |
| `zm_sv_zombie_max_immolator` | `5` | Max immolators alive at once (0 = none allowed when limits enabled) |

---

## Random Starting Weapons

Give all survivors a random weapon at the start of each round if the map loadout doesn't already provide one. The **Secondary** option gives only pistol-type weapons. The **Any** option picks randomly from Melee, Secondary, or Primary categories.

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_random_start_weapon` | `0` | 0=None, 1=Melee, 2=Secondary (pistols), 3=Primary, 4=Any |

---

## Create Server Settings

All ZMR settings are available in the **Game** tab of the Create Server dialog. No console required for server setup.

**Dropdowns:**
- **AI Zombie Master** — Disabled / Equal Chance / Fallback / Forced (`zm_sv_ai_zm`)
- **Bot Default Behavior** — Follow / Explore / Defend / Mixed Mode (`zm_sv_bot_default_behavior`, default: Mixed Mode)
- **Random Starting Weapon** — None / Melee / Secondary / Primary / Any (`zm_sv_random_start_weapon`)
- **AI ZM Spawner Access** — Global / Within View Only (`zm_sv_ai_zm_view_mode`, appears directly below AI Zombie Master)

**Checkboxes:**
- **Auto-Fill Survivor Bots** (`zm_sv_bot_survivors`, appears above Infinite Flashlight)
- **Infinite Flashlight** (`zm_sv_flashlight_infinite`)
- **Hidden Spawn All Classes** (`zm_sv_hidden_allclasses`)
- **Phys Explosion Ignite Barrels** (`zm_sv_physexp_ignite_barrels`)
- **Per-Zombie-Type Limits** (`zm_sv_zombie_type_limits`)
- **Allow Bot Possession** (`zm_sv_bot_possess`)

**Number/String Inputs:**
- ZM Resource Multiplier, Per-Player Resource Mult, Zombie Health/Damage Multipliers, Phys Explosion Player Damage, Hidden Spawn Cost Mult, AI ZM Difficulty Mult, AI ZM Trap Trigger Range, AI ZM Trap Cooldown, AI ZM Rush Prevention, Bot Weapon Search Range, Bot Help Voice Range, Max Zombies (default 70), and per-zombie-type limits (Banshees, Hulks, Drifters, Immolators)

---

## Hidden Spawn Upgrades

The ZM's hidden spawn power (place a zombie where no survivor can see) now supports all zombie types.

**For human ZMs:** While in hidden spawn mode, use the **scroll wheel** to cycle through zombie types from cheapest to most expensive. The cursor shows the correct zombie model for the selected class and displays friendly names (Shambler, Banshee, Hulk, Drifter, Immolator) with cost. Non-shambler types are only available when the server setting is enabled.

**For AI ZMs:** The AI automatically picks a random class when using hidden spawn.

**Cost:** Non-shambler hidden spawns cost extra based on the zombie's base cost multiplied by a configurable factor.

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_hidden_allclasses` | `0` | Allow all zombie types for hidden spawn (0=Shamblers only) |
| `zm_sv_hidden_cost_mult` | `0.25` | Extra cost multiplier for non-shambler hidden spawns |

---

## Kill Feed

When a human player is the Zombie Master, zombie kills now display the ZM player's actual name in the kill feed instead of just the zombie class name.

---

## UI Scale Support

Font definitions added for 1440p and 4K resolutions so the in-game UI scales correctly on high-DPI displays. The `zm_cl_ui_scale` ConVar (adjustable in the Graphics options menu) now scales the ZM resource/population HUD panel, including text positions and icon sizes.

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_cl_ui_scale` | `1.0` | UI scale multiplier (0.5–3.0). Adjustable via the Graphics options slider |
