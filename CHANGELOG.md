# Zombie Master: Reborn — Feature Changelog

This document summarizes all features and improvements added on top of the official ZMR GitHub source.

---

## AI Zombie Master (`zm_sv_ai_zm`)

The server can now run a fully automated Zombie Master when no human player volunteers (or always, depending on settings).

**How to enable:**
- `zm_sv_ai_zm 1` — Always use AI ZM
- `zm_sv_ai_zm 2` — Use AI ZM only when no human volunteers (fallback)
- `zm_sv_ai_zm 0` — Disabled (default)

**Persistence:** The AI ZM persists across rounds automatically. When a human player volunteers to be ZM, the AI ZM bot is kicked and a fresh survivor bot is spawned to fill the slot.

**Dynamic plan system:** The AI builds concrete multi-step spawn plans and executes them wave by wave. Each plan distributes spawns across all active spawners on the map using round-robin selection, so zombies appear from different locations and all players see action. Each wave picks a different zombie class than the previous one to keep the pressure varied. Cheap zombie types (shamblers) can appear in groups up to 25, while expensive types (banshees, hulks, etc.) are capped at 5 per wave.

**Trap opportunism:** The AI continuously monitors trap locations independently of its spawn plan. When survivors walk near a trap, it fires immediately regardless of what the current spawn plan is doing. Trap priority increases when survivors remain in range. Each trap has its own individual cooldown (default 30 seconds, configurable) so the AI won't spam the same trap repeatedly.

**Explosive barrels:** The AI watches for survivors near explosive barrels and will detonate them when someone gets within 200 units. This has a 5-minute cooldown to keep it from being overused.

The AI globally tracks all zombie spawners and traps on the map, activates them as needed, and rallies idle zombies toward the nearest survivors. The AI pauses between plans (configurable) so it doesn't produce a constant stream of zombies. Plan timing, aggression, spawn rate, and trap range are all configurable.

**Hidden spawn:** The AI occasionally places surprise zombies behind survivors using the hidden spawn system (same line-of-sight rules as human ZMs). It picks a random zombie class and searches for valid ground positions near a random survivor. The frequency scales with aggression. Hidden spawns are rate-limited to a configurable maximum per 2-minute window (default 5) to keep the focus on regular spawning.

**Resource management:** Traps now reserve resources for spawning — the AI will skip a trap if firing it would leave too few resources for the next spawn step. This prevents traps from starving the spawn plan.

**Spawner logging:** On the first plan build each round, the AI logs every spawner on the map (active or not) with its position, flags, active status, and supported zombie classes. This helps diagnose maps where spawners appear missing.

**Debug logging:** Enable with `zm_sv_ai_zm_debug 1` in the server console. Only the server host/admin can toggle this (cheat-protected). Logs plan construction, spawner discovery, step execution, trap triggers, hidden spawns, and rally commands.

**ConVars:**

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_ai_zm` | `0` | AI ZM mode: 0=off, 1=always, 2=fallback |
| `zm_sv_ai_zm_debug` | `0` | Verbose AI ZM console logging (cheat-protected) |
| `zm_sv_ai_zm_aggression` | `1.0` | Spawn rate/size multiplier (0.1–3.0) |
| `zm_sv_ai_zm_spawn_interval` | `8.0` | Minimum seconds between spawn waves |
| `zm_sv_ai_zm_spawn_batch` | `3` | Max zombies per wave |
| `zm_sv_ai_zm_trap_interval` | `15.0` | Minimum seconds between trap triggers |
| `zm_sv_ai_zm_trap_range` | `512` | Survivor proximity required to trigger a trap |
| `zm_sv_ai_zm_trap_cooldown` | `30.0` | Per-trap cooldown in seconds before the AI can re-use the same trap |
| `zm_sv_ai_zm_plan_pause` | `8.0` | Seconds to pause between completing one plan and starting the next |
| `zm_sv_ai_zm_hidden_max` | `5` | Maximum hidden spawns allowed per 2-minute window |
| `zm_sv_ai_zm_tactic_min_time` | `15.0` | Minimum seconds per plan before building a new one |
| `zm_sv_ai_zm_tactic_max_time` | `40.0` | Maximum seconds per plan before building a new one |
| `zm_sv_ai_zm_stall_timeout` | `12.0` | Seconds before abandoning a plan that can't execute |
| `zm_sv_ai_zm_rally_interval` | `6.0` | Seconds between idle zombie rally commands |
| `zm_sv_ai_zm_rally_buffer` | `256.0` | Distance buffer for splitting zombie targets between survivors |

---

## AI Survivor Bots (`zm_sv_bot_survivors`)

The server can automatically fill the survivor team with AI-controlled bots at round start.

**How to enable:** `zm_sv_bot_survivors <count>` — Set to the number of bots to spawn (0 = disabled).

**Bot behavior:**
- Bots follow the nearest **human** player by default, spreading out in a fan formation to avoid stacking. Bots never follow other bots. If no human survivors are alive (e.g. the only human is the ZM), bots automatically switch to defending the spawn area
- Press **E** while looking directly at a bot to toggle between **Following** and **Staying**. One press stays, one press follows — the bot shows a HUD message confirming the state
- If you are **holding an object** and press **E** on a bot, the bot will take the object from you and carry it instead of toggling follow/stay
- Bots automatically search for weapons and ammo on the ground (range configurable via `zm_sv_bot_weapon_search_range`), prioritizing missing loadout slots over ammo and higher-tier weapons over lower-tier. Ammo search triggers when at least one clip's worth is missing (not after every shot). After picking up an item, the bot returns to its original position
- Bots equip the best available weapon for the situation. When a ranged weapon runs out of ammo mid-combat, bots automatically switch to melee weapons or fists and actively chase nearby enemies (up to 512 units) instead of standing idle
- Bots look around naturally when idle — each bot waits a random delay after stopping before turning, and faces a different direction from its neighbors
- While following, bots periodically scan for nearby zombies (within 600 units) and turn to face threats
- Bots react to all nearby sounds (gunshots, zombie attacks, combat, footsteps, world sounds, bullet impacts) by looking toward the source when not already in combat
- Bots can shoot while moving when following a player (run-and-gun) instead of stopping to engage
- Bots engage enemies at appropriate range and fall back when enemies are too close
- Bots play **Alert** voicelines when fleeing from ZM explosions
- Bots play **Yes/acknowledgement** voicelines when given commands (follow, stay, defend, grab)
- Dead human players can possess a bot by pressing **USE** while spectating it (requires `zm_sv_bot_possess 1`)

**Voice command bot control:**
- **Help** (voice menu) — Nearby bots within `zm_sv_bot_help_range` come to the caller's location
- **Follow** (voice menu) — The bot the caller is looking at begins following the caller

**ConVars:**

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_bot_survivors` | `0` | Number of AI survivor bots to spawn per round |
| `zm_sv_bot_default_behavior` | `0` | Default bot mode: 0=Follow, 1=Explore, 2=Defend, 3=Mixed Mode |
| `zm_sv_bot_help_range` | `1024` | Range for Help voice command to affect bots |
| `zm_sv_bot_taunt_chance` | `8` | Percent chance for bots to play taunt sounds |
| `zm_sv_bot_possess` | `1` | Allow spectators to possess bots with USE key |
| `zm_sv_bot_weapon_search_range` | `1024` | How far bots search for weapons and ammo (units) |

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
| `zm_sv_zombie_max_banshee` | `0` | Max banshees alive at once (0 = none allowed when limits enabled) |
| `zm_sv_zombie_max_hulk` | `0` | Max hulks alive at once (0 = none allowed when limits enabled) |
| `zm_sv_zombie_max_drifter` | `0` | Max drifters alive at once (0 = none allowed when limits enabled) |
| `zm_sv_zombie_max_immolator` | `0` | Max immolators alive at once (0 = none allowed when limits enabled) |

---

## Random Starting Weapons

Give all survivors a random weapon at the start of each round. The **Secondary** option gives only pistol-type weapons. The **Any** option always gives something (nobody spawns empty-handed unless the map loadout already provides a weapon).

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_sv_random_start_weapon` | `0` | 0=None, 1=Melee, 2=Secondary (pistols), 3=Primary, 4=Any |

---

## Create Server Settings Tab

A dedicated **ZMR Settings** tab is available in the Create Server dialog, exposing all server ConVars listed above in a clean UI. Controls are grouped by type: dropdowns first, then checkboxes, then number/text inputs. No console required for server setup.

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

## Mixed Mode (Bot Behavior)

The "Mixed Mode" bot behavior option (value 3) randomly assigns each bot one of three behaviors at spawn: Follow a player, Explore the map, or Defend the spawn area. This creates more varied and realistic bot movement patterns.

---

## UI Scale Support

Font definitions added for 1440p and 4K resolutions so the in-game UI scales correctly on high-DPI displays. The `zm_cl_ui_scale` ConVar (adjustable in the Graphics options menu) now scales the ZM resource/population HUD panel, including text positions and icon sizes.

| ConVar | Default | Description |
|--------|---------|-------------|
| `zm_cl_ui_scale` | `1.0` | UI scale multiplier (0.5–3.0). Adjustable via the Graphics options slider |
