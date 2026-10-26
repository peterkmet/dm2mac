# Items

The values of every item of `GRAPHICS.DAT` (PC DOS, English), by category. The game reads them from the GDAT value keys `(category, index, 0x0b, entry)` with `item_attr`; the names and the actions are the texts `(category, index, 5, entry)`. The tables use the GDAT names; a rune glyph of a name shows as the upper-case rune name (`VI POTION`).

## Columns

| Column | GDAT entry | Meaning |
|---|---|---|
| Index | | The GDAT index of the item in its category. |
| Weight (kg) | `0x01` | The weight, in tenths of a kilogram in the data. |
| Value | `0x02` | The value in copper coins (copper coin 1, silver coin 4, gold coin 16). Merchants change it with their price factors. |
| Strength | `0x08` | The melee bonus: a melee action adds it to the champion strength ([champions.md](../rules/champions.md)). |
| Throw | `0x09` | The throw power: a throw or a shot adds it. |
| Worn on | `0x04` | The body slots that the clothing fits. |
| Armour | `0x0b` bits 0–7 | The defense of the clothing. |
| Sharp | `0x0b` bits 8–10 | The sharp factor: against a sharp attack the defense is armour × (sharp + 4) / 8. |
| Effect | | The effect of a potion when a champion drinks it; the power of the potion sets the amount. |
| Actions | texts `0x08`–`0x0b` | The actions of the item in the action hand (see [Actions](#actions)). |
| Special | | Flags and modifiers (see [Special](#special)). |

## Actions

An action shows its name, then its effect, the skill that it trains and needs, and its charges.

- `damage ×n`: the damage factor of a melee action. The hit damage is the champion strength (with the weapon strength) × n, less the defense of the creature.
- A spell action names its missile or its aura and the power.
- `level n`: the action shows only from this skill level.
- `n charges`: the action needs and uses n charges of the item.
- The skills are the 4 base skills (fighter, ninja, priest, wizard) and their 16 sub-skills, 4 for each base skill: swing, thrust, club, parry; steal, fight, throw, shoot; identify, heal, influence, defend; fire, air, earth, water. The names thrust, club, identify, defend, fire, air, earth, and water are the sub-skill names of Dungeon Master; they are unverified for Dungeon Master II, because the port has no names for these skills.

## Special

- `extra damage n`: a hit adds armour-piercing damage against creatures; against champions it is poison.
- `gives light in a hand by its charges`: the item lights the party in a hand, and its charges burn down.
- `worn or held`: a modifier counts when the item is in a slot that fits it. `in any slot`: in every slot. `when eaten`: once, when a champion eats or drinks it; a skill or speed change of it ends after the given steps.
- `vanishes with its last charge`: the item goes away when an action uses its last charge.

## Weapons

| Index | Name | Weight (kg) | Value | Strength | Throw | Actions | Special |
|---|---|---|---|---|---|---|---|
| 0 | EYE OF TIME | 0.1 | 2200 |  |  | FREEZE (freezes the creatures, power 80, influence, 3 charges)<br>LO OH IR ROS (haste, power 40, water, 1 charge) |  |
| 1 | STORMRING | 0.1 | 810 |  |  | PUNCH (damage ×0.81, swing)<br>MON OH KATH RA (lightning bolt, power 180, air level 4, 1 charge) | value per charge 8 |
| 2 | TORCH | 1.1 | 2 | 8 |  | SWING (damage ×0.50, needs no charges left)<br>SWING (damage ×0.50, full damage to half-damage creatures, needs a charge, uses none) | gives light in a hand by its charges |
| 3 | KALAN GAUNTLET | 2 | 1700 | 20 |  | PUNCH (damage ×1.19, swing)<br>PAL OH KATH ROS (pull, power 180, earth, 1 charge)<br>PAL OH KATH KU (push, power 150, club level 4) | maximum mana +20 worn or held; fight skill +2 worn or held; value per charge 10 |
| 4 | ROGUE STAVE | 0.9 | 64 | 16 |  | PARRY (damage ×0.25, thrust)<br>BASH (damage ×0.81, fight)<br>CONFUSE (frightens the creature in front, power 12, influence level 3) | maximum mana +13 worn or held; value per charge 4; does not go into a container |
| 5 | TEMPEST | 3 | 776 | 49 | 8 | JAB (damage ×0.94, thrust)<br>CHOP (damage ×1.81, swing)<br>PAL OH KATH RA (lightning bolt, power 180, air level 3, 1 charge) | gives light in a hand by its charges; value per charge 6; does not go into a container |
| 6 | FURY | 4.7 | 1100 | 55 |  | CHOP (damage ×1.50, full damage to half-damage creatures, swing)<br>MELEE (damage ×1.69, full damage to half-damage creatures, swing level 4)<br>EE FUL IR (fireball, power 120, fire, 1 charge) | gives light in a hand by its charges; value per charge 8; does not go into a container |
| 7 | SCYTHE | 2.6 | 20 | 20 |  | SWING (damage ×0.56, swing) | does not go into a container |
| 8 | DAGGER | 0.5 | 10 | 10 | 50 | SLASH (damage ×0.72, fight)<br>STAB (damage ×1.50, fight)<br>THROW (throws, throw) | fight skill +1 worn or held |
| 9 | MACHETE | 2.8 | 40 | 30 |  | SWING (damage ×0.66, swing)<br>CHOP (damage ×1.25, swing) | does not go into a container |
| 10 | SWORD | 3.2 | 72 | 34 |  | SWING (damage ×0.75, swing)<br>CHOP (damage ×1.19, swing)<br>THRUST (damage ×2.00, thrust) | does not go into a container |
| 11 | RAPIER | 2.6 | 100 | 38 |  | JAB (damage ×0.62, thrust)<br>THRUST (damage ×1.50, thrust level 4)<br>LUNGE (damage ×2.06, thrust level 4) | does not go into a container |
| 12 | SABRE | 3.3 | 160 | 42 |  | SLASH (damage ×0.75, fight)<br>PARRY (damage ×0.50, thrust)<br>MELEE (damage ×1.75, swing level 5) | does not go into a container |
| 13 | KATANA | 3.5 | 204 | 46 |  | SLASH (damage ×0.88, swing)<br>CHOP (damage ×1.50, swing)<br>MELEE (damage ×1.88, swing level 5) | does not go into a container |
| 14 | EXCSYMYR | 3.3 | 350 | 50 |  | CHOP (damage ×1.50, swing)<br>THRUST (damage ×2.38, thrust level 4)<br>MELEE (damage ×2.12, swing level 7) | maximum mana +10 worn or held; thrust skill +1 worn or held; does not go into a container |
| 15 | BLUE STEELE | 3.7 | 500 | 60 |  | SLICE (damage ×1.25, swing)<br>CLEAVE (damage ×1.81, swing)<br>SEVER (damage ×2.38, swing level 8) | swing skill +1 worn or held; does not go into a container |
| 16 | VORPAL BLADE | 3 | 400 | 48 |  | JAB (damage ×0.62, pierces, thrust)<br>CLEAVE (damage ×1.25, pierces, swing level 3)<br>DISRUPT (damage ×2.00, pierces, fire level 4) | does not go into a container |
| 17 | ZO BLADE | 1 | 900 | 45 |  | MON ZO (open door, power 240, air)<br>ZO CUT (damage ×1.25, air)<br>ZO CLEAVE (damage ×2.81, air level 5) |  |
| 18 | AXE | 4.3 | 120 | 49 |  | SWING (damage ×0.94, swing)<br>CHOP (damage ×1.62, swing)<br>MELEE (damage ×2.00, swing level 5) | does not go into a container |
| 19 | VORAX | 6.5 | 660 | 70 |  | CHOP (damage ×1.81, swing)<br>CLEAVE (damage ×2.44, swing level 3)<br>BERZERK (damage ×3.00, swing level 7) | extra damage 10; swing skill +1 worn or held; does not go into a container |
| 20 | MACE | 3.1 | 60 | 42 |  | SWING (damage ×0.88, swing)<br>BASH (damage ×2.12, club)<br>STUN (damage ×1.06, club level 4) | does not go into a container |
| 21 | TECHMACE | 4.1 | 376 | 65 |  | BASH (damage ×2.12, club)<br>STUN (damage ×0.94, club level 4)<br>POWERSWING (damage ×2.25, club level 5, 1 charge) | club skill +1 worn or held; does not go into a container |
| 22 | MORNINGSTAR | 5 | 140 | 60 |  | SWING (damage ×1.44, club)<br>STUN (damage ×0.84, club level 3)<br>MELEE (damage ×2.12, club level 4) |  |
| 23 | CLUB | 3.6 | 4 | 19 | 30 | BASH (damage ×1.09, club)<br>THROW (throws, throw) | does not go into a container |
| 24 | WAR CLUB | 6 | 15 | 44 | 55 | BASH (damage ×1.88, club)<br>BLUDGEON (damage ×2.31, club level 5)<br>THROW (throws, throw) | club skill +1 worn or held; does not go into a container |
| 25 | BOW | 1 | 20 |  | 55 | SHOOT (shoots, shoot) | launcher; shoot attack 150; does not go into a container |
| 26 | CROSSBOW | 2.8 | 80 |  | 70 | SHOOT (shoots, shoot level 1) | launcher; shoot attack 165; does not go into a container |
| 27 | ARROW | 0.2 | 3 |  | 60 | STAB (damage ×0.88, fight)<br>THROW (throws, throw) | shoot attack 5 |
| 28 | SLAYER ARROW | 0.2 | 6 |  | 100 | STAB (damage ×1.50, fight)<br>THROW (throws, throw) | shoot attack 5 |
| 29 | SLING | 1.9 | 5 |  | 20 | SHOOT (shoots, shoot) | launcher; shoot attack 50 |
| 30 | ROCK | 1 | 0 |  | 20 | THROW (throws, throw) | shoot attack 1 |
| 31 | POISON DART | 0.3 | 5 |  | 40 | THROW (throws, throw) | shoot attack 8; extra damage 20 |
| 32 | SHURIKEN | 0.1 | 7 |  | 20 | THROW (throws, throw) | shoot attack 2 |
| 33 | BRANCH | 0.8 | 0 | 7 |  | SWING (damage ×0.50, swing)<br>THROW (throws, throw) |  |
| 34 | STAFF | 2.6 | 7 | 12 |  | SWING (damage ×0.56, swing)<br>UM FUL (light, power 60, air)<br>LO ZO (open door, power 20, air, 1 charge) | maximum mana +5 worn or held; does not go into a container |
| 35 | RAINBOW WAND | 0.1 | 44 |  |  | CALM (frightens the creature in front, power 7, influence)<br>ON OH IR RA (light, power 250, air, 1 charge)<br>EE OH EW DAIN (wisdom aura, power 140, identify, 1 charge) | maximum mana +10 worn or held; value per charge 3 |
| 36 | SPIRAL STAFF | 3 | 104 | 9 |  | FREEZE (freezes the creatures, power 40, influence, 1 charge)<br>MON OH EW ROS (dexterity aura, power 213, defend, 2 charges)<br>UM OH KATH RA (lightning bolt, power 80, air, 1 charge) | maximum mana +15 worn or held; value per charge 6; does not go into a container |
| 37 | EMERALD ORB | 2.9 | 260 | 15 |  | CALM (frightens the creature in front, power 180, influence level 3, 1 charge)<br>HEAL (heals with mana, earth)<br>PAL DES VEN (poison bolt, power 180, water level 3, 1 charge) | maximum mana +30 worn or held; water skill +1 worn or held; value per charge 8; does not go into a container |
| 38 | COMBAT STAFF | 3.5 | 380 | 40 |  | SWING (damage ×1.00, swing)<br>HEAL (heals with mana, earth level 5, 1 charge)<br>MELEE (damage ×1.56, swing level 5) | maximum mana +20 worn or held; fighter skill +1 worn or held; value per charge 6; does not go into a container |
| 39 | STAFF OF NETA | 2.1 | 154 |  |  | HEAL (heals with mana, earth)<br>PAL OH EW NETA (vitality aura, power 170, heal, 1 charge)<br>ON FUL BRO NETA (fire shield, power 84, defend) | maximum mana +25 worn or held; influence skill +1 worn or held; does not go into a container |
| 40 | SERPENT STAFF | 3.3 | 340 |  |  | UM YA IR DAIN (spell shield, power 80, defend level 2)<br>UM FUL BRO NETA (fire shield, power 80, defend level 3)<br>PAL DES EW (harm non-material, power 150, fire level 4, 1 charge) | maximum mana +10 worn or held; earth skill +1 worn or held; value per charge 11; does not go into a container |
| 41 | BAINBLOOM | 0.8 | 20 |  |  | SWING (damage ×0.50, swing)<br>EE OH VEN (poison cloud, power 60, fire level 3, 1 charge) | maximum mana +6 worn or held; air skill +2 worn or held; value per charge 4; does not go into a container |
| 42 | NUMENSTAFF | 3.6 | 1212 |  |  | PARRY (damage ×0.25, thrust)<br>ON DES IR SAR (light, power 200, air)<br>UM FUL IR (fireball, power 75, fire level 2, 1 charge) | maximum mana +40 worn or held; fire skill +1 worn or held; value per charge 10; does not go into a container |
| 43 | SCARAB | 0.8 | 180 |  |  | MON YA IR (defense shield, power 250, defend, uses all charges) | vanishes with its last charge; maximum mana +5 worn or held; luck +5 worn or held |
| 44 | SPEEDBOW | 3 | 180 |  | 90 | SHOOT (shoots, shoot) | launcher; shoot attack 200; does not go into a container |
| 45 | METEOR METAL | 4.4 | 80 | 20 |  | SWING (damage ×0.34, swing)<br>CHOP (damage ×0.62, swing) | does not go into a container |

## Clothing

| Index | Name | Weight (kg) | Value | Worn on | Armour | Sharp | Actions | Special |
|---|---|---|---|---|---|---|---|---|
| 0 | CAPE | 0.3 | 6 | neck, torso |  |  |  |  |
| 1 | CLOAK OF NIGHT | 0.4 | 25 | neck, torso |  |  |  | dexterity +10 worn or held |
| 2 | BLUE PANTS | 0.7 | 18 | legs | 12 | 2 |  |  |
| 3 | SANDALS | 0.6 | 3 | feet | 4 | 1 |  |  |
| 4 | BLACK BOOTS | 1.6 | 20 | feet | 25 | 4 |  |  |
| 5 | BLACK TOP | 0.4 | 12 | torso | 5 | 1 |  |  |
| 6 | BLACK SKIRT | 0.4 | 12 | legs | 5 | 1 |  |  |
| 7 | FINE ROBE TOP | 0.3 | 20 | torso | 7 | 2 |  |  |
| 8 | FINE ROBE | 0.3 | 20 | legs | 7 | 2 |  |  |
| 9 | FIRE PLATE | 4.7 | 684 | torso | 60 | 6 |  | fighter skill +1 worn or held; does not go into a container |
| 10 | BODICE | 0.2 | 24 | torso | 4 | 1 |  | influence skill +1 worn or held |
| 11 | TABARD | 0.4 | 8 | legs | 5 | 1 |  |  |
| 12 | GUNNA | 0.5 | 17 | legs | 7 | 1 |  |  |
| 13 | DOUBLET | 0.3 | 30 | torso | 12 | 2 |  |  |
| 14 | HUKE | 0.3 | 28 | legs | 13 | 2 |  |  |
| 15 | MITHRAL HOSEN | 0.1 | 180 | feet | 40 | 2 |  | move speed +1 worn or held |
| 16 | LEATHER JERKIN | 1.1 | 47 | torso | 17 | 3 |  |  |
| 17 | LEATHER PANTS | 1 | 35 | legs | 17 | 3 |  |  |
| 18 | LEATHER BOOTS | 1.8 | 12 | feet | 20 | 3 |  |  |
| 19 | BRIGANDINE | 1.7 | 76 | torso | 25 | 2 |  | steal skill +1 worn or held |
| 20 | TUNIC | 0.5 | 16 | torso | 9 | 1 |  |  |
| 21 | SCALE HAUBERK | 5.4 | 160 | torso | 45 | 4 |  |  |
| 22 | SCALE MAIL | 4.1 | 96 | legs | 35 | 4 |  |  |
| 23 | BANDANA | 0.1 | 1 | head | 1 | 4 |  | heal skill +1 worn or held |
| 24 | GREAT HELM | 2 | 140 | head | 30 | 4 |  | influence skill +1 worn or held |
| 25 | HORNED HELM | 0.9 | 22 | head | 12 | 5 |  |  |
| 26 | HELMET | 1.2 | 60 | head | 17 | 5 |  |  |
| 27 | BASCINET | 1.5 | 88 | head | 20 | 5 |  |  |
| 28 | SHIELD OF FIRE | 4 | 400 |  | 180 | 5 | BLOCK (defends, fight level 1)<br>PARRY (damage ×0.69, thrust)<br>EE OH EW KU (strength aura, power 150, fire, 1 charge) | anti-fire +10 worn or held; does not go into a container |
| 29 | CRYSTAL SHIELD | 2.9 | 172 |  | 60 | 7 | BLOCK (defends, fight level 1)<br>PARRY (damage ×0.25, thrust)<br>PAL YA IR DAIN (spell shield, power 170, defend level 2, 1 charge) | does not go into a container |
| 30 | WOOD SHIELD | 1.4 | 10 |  | 20 | 3 | BLOCK (defends, fight)<br>PARRY (damage ×0.25, thrust) | does not go into a container |
| 31 | SMALL SHIELD | 2.1 | 44 |  | 35 | 4 | BLOCK (defends, fight)<br>PARRY (damage ×0.50, thrust) | does not go into a container |
| 32 | BREASTPLATE | 4.5 | 124 | torso | 35 | 4 |  | does not go into a container |
| 33 | THIGH PLATES | 3.3 | 60 | legs | 25 | 4 |  | does not go into a container |
| 34 | MITHRAL MAIL | 5.2 | 352 | torso | 70 | 5 |  | luck +5 worn or held |
| 35 | MITHRAL HUKE | 4.1 | 260 | legs | 60 | 5 |  | luck +5 worn or held |
| 36 | MAIL HELMET | 1.6 | 42 | head | 30 | 5 |  |  |
| 37 | GREAVES | 1.6 | 45 | feet | 30 | 4 |  |  |
| 38 | ARMET | 1.9 | 200 | head | 40 | 7 |  |  |
| 39 | TORSO PLATE | 12 | 264 | torso | 65 | 5 |  | does not go into a container |
| 40 | LEG PLATE | 8 | 125 | legs | 56 | 5 |  | does not go into a container |
| 41 | FOOT PLATE | 2.8 | 84 | feet | 37 | 5 |  |  |
| 42 | SAR SHIELD | 3.4 | 200 |  | 56 | 4 | BLOCK (defends, fight)<br>PARRY (damage ×0.25, thrust)<br>HIT (damage ×0.56, fight level 2) | priest skill -1 worn or held; does not go into a container |
| 43 | RASAR HELM | 1.7 | 300 | head | 62 | 5 |  | dexterity +15 worn or held |
| 44 | RASAR PLATE | 7.5 | 700 | torso | 125 | 6 |  | maximum mana +20 worn or held; does not go into a container |
| 45 | RASAR POLEYN | 6.2 | 400 | legs | 90 | 6 |  | luck +5 worn or held; does not go into a container |
| 46 | RASAR GREAVE | 2.2 | 200 | feet | 50 | 6 |  |  |
| 47 | RASAR SHIELD | 3.2 | 300 |  | 85 | 7 | BLOCK (defends, fight)<br>PARRY (damage ×0.38, thrust)<br>LO OH EW SAR (invisibility, power 41, air level 5, 1 charge) | does not go into a container |
| 48 | TECHELM | 3.5 | 590 | head | 76 | 4 |  | wisdom +15 worn or held |
| 49 | TECHPLATE | 14.1 | 1250 | torso | 160 | 4 |  | anti-magic +30 worn or held; does not go into a container |
| 50 | TECHPOLEYN | 9 | 600 | legs | 101 | 4 |  | does not go into a container |
| 51 | TECHBOOTS | 3.1 | 300 | feet | 60 | 5 |  |  |
| 52 | TECHSHIELD | 4 | 348 |  | 100 | 7 | BLOCK (defends, fight level 1)<br>UM ZO BRO ROS (explosion at the party, power 4, water)<br>TELEPORT (warps to the marker) | anti-magic +10 worn or held; does not go into a container |
| 53 | FIRE HELM | 2.1 | 400 | head | 54 | 6 |  | anti-fire +10 worn or held |
| 54 | FIRE PLATE | 10.8 | 648 | torso | 60 | 6 |  | fighter skill +1 worn or held; does not go into a container |
| 55 | FIRE POLEYN | 8.1 | 500 | legs | 88 | 4 |  | strength +15 worn or held; does not go into a container |
| 56 | FIRE GREAVE | 2.5 | 220 | feet | 16 | 5 |  | anti-fire +4 worn or held |

## Scrolls

| Index | Name | Weight (kg) | Value |
|---|---|---|---|
| 0 | SCROLL | 0.1 | 1 |

## Potions

| Index | Name | Weight (kg) | Value | Effect | Actions |
|---|---|---|---|---|---|
| 3 | VEN BOMB | 0.2 | 160 |  | THROW (throws, throw) |
| 6 | OH BRO ROS POTION | 0.3 | 36 | raises dexterity | DRINK (eat or drink) |
| 7 | FUL BRO KU POTION | 0.3 | 48 | raises strength | DRINK (eat or drink) |
| 8 | YA BRO DAIN POTION | 0.3 | 40 | raises wisdom | DRINK (eat or drink) |
| 9 | YA BRO NETA POTION | 0.3 | 44 | raises vitality | DRINK (eat or drink) |
| 10 | VI BRO POTION | 0.3 | 20 | cures poison | DRINK (eat or drink) |
| 11 | YA POTION | 0.3 | 32 | stamina | DRINK (eat or drink) |
| 12 | YA BRO POTION | 0.3 | 28 | defense aura | DRINK (eat or drink) |
| 13 | ZO BRO RA POTION | 0.3 | 28 | mana | DRINK (eat or drink) |
| 14 | VI POTION | 0.3 | 24 | health, heals wounds | DRINK (eat or drink) |
| 15 | WATER FLASK | 0.3 | 16 | water | DRINK (eat or drink) |
| 19 | FUL BOMB | 0.5 | 220 |  | THROW (throws, throw) |
| 20 | FLASK | 0.1 | 16 |  |  |

## Containers

| Index | Name | Weight (kg) | Value | Actions | Special |
|---|---|---|---|---|---|
| 0 | CHEST | 5.6 | 22 |  | does not go into a container |
| 1 | BAG | 0.7 | 5 |  | does not go into a container |
| 2 | MONEY BOX | 1.1 | 35 |  |  |
| 3 | QUIVER | 0.7 | 5 |  | does not go into a container |
| 8 | MAGIC MAP | 0.3 | 100 | S (minion attacks)<br>K (dismisses the minion) |  |
| 9 | SCOUT MAP | 0.3 | 300 | S (minion attacks)<br>K (dismisses the minion) |  |
| 10 | MINION MAP | 0.3 | 625 | C (minion guards)<br>F (minion fetches)<br>M (sets the minion target)<br>K (dismisses the minion) |  |
| 11 | MAGIC MAP | 0.3 | 100 | S (minion attacks)<br>K (dismisses the minion) |  |

## Miscellaneous items

| Index | Name | Weight (kg) | Value | Actions | Special |
|---|---|---|---|---|---|
| 0 | BONES | 1.5 | 0 |  |  |
| 1 | BOTA | 0.9 | 2 | DRINK (eat or drink, needs a charge, uses none) | holds water; weight per charge 0.3 kg |
| 2 | JEWEL OF SYMAL | 0.2 | 54 |  | anti-fire +6 worn or held |
| 3 | ILLUMULET | 0.2 | 89 |  | gives light in a hand by its charges |
| 5 | COMPASS | 0.1 | 10 |  |  |
| 6 | COPPER COIN | 0.1 | 1 |  | coin |
| 7 | SMALL PLANK | 0.4 | 0 |  |  |
| 8 | GOLD COIN | 0.1 | 16 |  | coin |
| 9 | IRON KEY | 0.1 | 1 |  |  |
| 10 | BLOOD KEY | 0.1 | 2 |  |  |
| 11 | SOLID KEY | 0.1 | 2 |  |  |
| 12 | MOON KEY | 0.1 | 1 |  |  |
| 13 | LIGHTNING KEY | 0.1 | 3 |  |  |
| 14 | CROSS KEY | 0.1 | 2 |  |  |
| 15 | ONYX KEY | 0.1 | 3 |  |  |
| 16 | SKULL KEY | 0.1 | 175 |  |  |
| 17 | GOLD KEY | 0.1 | 46 |  |  |
| 18 | IR KEY | 0.1 | 120 |  |  |
| 19 | YA KEY | 0.1 | 30 |  |  |
| 20 | OH KEY | 0.1 | 36 |  |  |
| 21 | VI KEY | 0.1 | 44 |  |  |
| 22 | FUL KEY | 0.1 | 60 |  |  |
| 23 | RA KEY | 0.1 | 27 |  |  |
| 24 | MASTER KEY | 0.1 | 38 |  |  |
| 25 | BOULDER | 9 | 0 |  |  |
| 26 | BLUE GEM | 0.1 | 1024 |  | coin |
| 27 | RED GEM | 0.1 | 256 |  | coin |
| 28 | GREEN GEM | 0.1 | 64 |  | coin |
| 29 | APPLE | 0.4 | 2 | EAT (eat or drink) | food 400 |
| 30 | JICAMA | 0.5 | 3 | EAT (eat or drink) | holds water; vanishes with its last charge; food 400 |
| 31 | BREAD | 0.3 | 3 | EAT (eat or drink) | food 650 |
| 32 | CHEESE | 0.8 | 4 | EAT (eat or drink) | food 820 |
| 33 | VACUUM FUSE | 0.5 | 350 |  |  |
| 34 | SPIRIT CAP | 0.3 | 120 | EAT (eat or drink) | food 1; vitality -28 when eaten; wizard skill +2 when eaten; the eaten effect lasts 400 steps |
| 35 | SHANK | 0.4 | 6 | EAT (eat or drink) | food 990 |
| 36 | STEAK | 0.6 | 10 | EAT (eat or drink) | food 1400 |
| 37 | MEZMAR | 0.2 | 188 |  | dexterity +5 worn or held |
| 38 | SUZERAIN | 0.3 | 420 |  | maximum mana +10 worn or held |
| 39 | MOONSTONE | 0.2 | 130 |  | maximum mana +3 worn or held |
| 40 | RUNE CHARM | 0.2 | 27 |  | luck +4 worn or held |
| 41 | CLAN CHIEF GEM | 0.2 | 268 |  |  |
| 42 | METEOR METAL | 2.6 | 470 |  | luck +4 worn or held; does not go into a container |
| 43 | MAGIKAL BOX | 0.9 | 190 | FREEZE (freezes the creatures, power 30, influence, uses all charges)<br>LO OH IR ROS (haste, power 60, water, uses all charges) | vanishes with its last charge |
| 44 | LARGE GEAR | 3.5 | 16 |  |  |
| 45 | ROPE | 1.5 | 8 | DESCEND (descends into a pit, steal) |  |
| 47 | MANA BLOSSOM | 2 | 50 | EAT (eat or drink) | food 200; maximum mana +15 when eaten; light +25 worn or held |
| 48 | SUN CREST | 2 | 1 |  | does not go into a container |
| 49 | CLAN KEY PIECE | 0.1 | 0 |  |  |
| 50 | PALMAPPLE | 1.6 | 12 | EAT (eat or drink) | food 500; fighter skill +1 when eaten; the eaten effect lasts 600 steps |
| 51 | FAIRY CUSHION | 0.3 | 80 | EAT (eat or drink) | food 50; strength +25 when eaten; vitality -118 when eaten |
| 52 | BONES | 1.5 | 0 |  |  |
| 53 | PLANK | 1.8 | 2 |  |  |
| 54 | COVER PLATE | 8.5 | 12 |  | does not go into a container |
| 55 | TAPESTRY | 1.2 | 24 |  | does not go into a container |
| 56 | SILVER COIN | 0.1 | 4 |  | coin |
| 57 | WORM ROUND | 1 | 2 | EAT (eat or drink) | food 400 |
| 58 | PYRO | 8.1 | 10 |  |  |
| 59 | DEAD BAT | 0.6 | 4 | EAT (eat or drink) | food 250 |
| 60 | TANKARD | 0.5 | 4 | DRINK (eat or drink, needs a charge, uses none) | holds water; weight per charge 0.3 kg |
| 61 | BONE | 1.5 | 0 |  |  |
| 62 | YA EW | 1.5 | 0 |  |  |
| 63 | ATTACK MINION | 1 | 240 | MON ZO EW KU (summons a minion, fire, uses all charges) | vanishes with its last charge |
| 64 | GUARD MINION | 1 | 200 | EE ZO EW NETA (summons a minion, earth, uses all charges) | vanishes with its last charge |
| 66 | TECHEYE | 3 | 0 |  |  |
