"""The document's names for the values its lists define.

Purpose:
    Map a raw value to the name the format description (Mission_XvT.txt by
    Michael Gaisser, GNU Free Documentation License 1.3 or later) gives it,
    for the JSON export. These are names only; the document's notes are not
    reproduced. The text renderer uses its own vocabulary (``render.py``).

Flow:
    ``name_of(list_name, value)`` looks the value up in ``LISTS``.

Invariants:
    - Keys are the raw stored values (DesignationTeam keys are the stored
      character codes).
    - A "*" (questionable) marker and parenthesised notes after a double space
      are dropped from names; everything else is as the document spells it.

Call:
    ``name_of("CraftType", 1)`` returns ``"X-wing"``.
"""

from __future__ import annotations

import logging

logger = logging.getLogger(__name__)

PLATFORMID: dict[int, str] = {12: "XvT", 14: "BoP"}
MISSIONTYPE: dict[int, str] = {
    0x00: "Training",
    0x01: "Unknown",
    0x02: "Melee",
    0x03: "Multiplayer Training",
    0x04: "Multiplayer Combat",
}
GOALARGUMENT: dict[int, str] = {
    0x00: "must",
    0x01: "must NOT",
    0x02: "BONUS must",
    0x03: "BONUS must NOT",
}
COUNTERMEASURES: dict[int, str] = {
    0x00: "None",
    0x01: "Chaff",
    0x02: "Flare",
    0x03: "Cluster Mine",
}
OPTIONALCRAFTCATEGORY: dict[int, str] = {
    0x00: "None",
    0x01: "All Flyable",
    0x02: "All Rebel Flyable",
    0x03: "All Imperial Flyable",
    0x04: "User Defined",
}

DESIGNATIONTEAM: dict[int, str] = {
    0x31: "Team1",
    0x32: "Team2",
    0x33: "Team3",
    0x34: "Team4",
    0x35: "Team5",
    0x36: "Team6",
    0x37: "Team7",
    0x38: "Team8",
    0x41: "All",
    0x4F: "Others",
    0x48: "Hostiles",
    0x46: "Friendly",
    0xFF: "None",
}
DESIGNATION: dict[int, str] = {
    0xFF: "NONe",
    0x00: "COMmand Ship",
    0x01: "BASe",
    0x02: "STAtion",
    0x03: "MISsion Critical Craft",
    0x04: "CONvoy Craft",
    0x05: "STRike Craft",
    0x06: "RELoad Craft",
    0x07: "PRImary Target",
    0x08: "SECondary Target",
    0x09: "TERtiary Target",
    0x0A: "RESearch Facility",
    0x0B: "MANufacturing Facility",
}
CRAFTTYPE: dict[int, str] = {
    0x00: "None",
    0x01: "X-wing",
    0x02: "Y-wing",
    0x03: "A-wing",
    0x04: "B-wing",
    0x05: "TIE Fighter",
    0x06: "TIE Interceptor",
    0x07: "TIE Bomber",
    0x08: "TIE Advanced",
    0x09: "TIE Defender",
    0x0A: "Unused",
    0x0B: "Unused",
    0x0C: "Missile Boat",
    0x0D: "T-wing",
    0x0E: "Z-95 Headhunter",
    0x0F: "R-41 Starchaser",
    0x10: "Assault Gunboat",
    0x11: "Shuttle",
    0x12: "Escort Shuttle",
    0x13: "System Patrol Craft",
    0x14: "Scout Craft",
    0x15: "Stormtrooper Transport",
    0x16: "Assault Transport",
    0x17: "Escort Transport",
    0x18: "Tug",
    0x19: "Combat Utility Vehicle",
    0x1A: "Container A",
    0x1B: "Container B",
    0x1C: "Container C",
    0x1D: "Container D",
    0x1E: "Heavy Lifter",
    0x1F: "Unused",
    0x20: "Bulk Freighter",
    0x21: "Cargo Ferry",
    0x22: "Modular Conveyor",
    0x23: "Container Transport",
    0x24: "Medium Transport",
    0x25: "Murrian Transport",
    0x26: "Corellian Transport",
    0x27: "Unused",
    0x28: "Corellian Corvette",
    0x29: "Modified Corvette",
    0x2A: "Nebulon-B Frigate",
    0x2B: "Modified Frigate",
    0x2C: "C-3 Passenger Liner",
    0x2D: "Carrack Cruiser",
    0x2E: "Strike Cruiser",
    0x2F: "Escort Carrier",
    0x30: "Dreadnaught",
    0x31: "Mon Calamari Cruiser",
    0x32: "Light Mon Calamari Cruiser",
    0x33: "Interdictor Cruiser",
    0x34: "Victory-class Star Destroyer",
    0x35: "Imperator-class Star Destroyer",
    0x36: "Executor-class Star Destroyer",
    0x37: "Container E",
    0x38: "Container F",
    0x39: "Container G",
    0x3A: "Container H",
    0x3B: "Container I",
    0x3C: "Platform A",
    0x3D: "Platform B",
    0x3E: "Platform C",
    0x3F: "Platform D",
    0x40: "Platform E",
    0x41: "Platform F",
    0x42: "Asteroid R&D Station",
    0x43: "Asteroid Laser Battery",
    0x44: "Asteroid Warhead Battery",
    0x45: "X/7 Factory",
    0x46: "Satellite 1",
    0x47: "Satellite 2",
    0x48: "Unused",
    0x49: "Unused",
    0x4A: "Unused",
    0x4B: "Mine A",
    0x4C: "Mine B",
    0x4D: "Mine C",
    0x4E: "Gun Emplacement",
    0x4F: "Unused",
    0x50: "Probe A",
    0x51: "Probe B",
    0x52: "Unused",
    0x53: "Nav Buoy A",
    0x54: "Nav Buoy B",
    0x55: "Unused",
    0x56: "Asteroid Field",
    0x57: "Planet",
    0x58: "Unused",
    0x59: "Unused",
    0x5A: "Shipyard",
    0x5B: "Repair Yard",
    0x5C: "Modified Strike Cruiser",
}
STATUS: dict[int, str] = {
    0x00: "None",
    0x01: "2X Warheads",
    0x02: "1/2 Warheads",
    0x03: "No Shields",
    0x04: "1/2 Shields",
    0x05: "No Turrets",
    0x06: "No Hyperdrive",
    0x07: "Shields 0%, charging",
    0x08: "Shields added or 200%",
    0x09: "Hyperdrive added",
    0x0A: "Unknown",
    0x0B: "Unknown",
    0x0C: "(200% Shields)",
    0x0D: "Shields 50%, Charging",
    0x0E: "(No Turrets)",
    0x0F: "Unknown",
    0x10: "Shields + Hyperdrive added",
    0x11: "Unknown",
    0x12: "200% Shields",
    0x13: "(50% Shields)",
    0x14: "Invincible",
    0x15: "Infinite Ammo",
}
WARHEAD: dict[int, str] = {
    0x00: "None",
    0x01: "Space Bomb",
    0x02: "Heavy Rocket",
    0x03: "Concussion Missile",
    0x04: "Torpedo",
    0x05: "Advanced Concussion Missile",
    0x06: "Advanced Torpedo",
    0x07: "Mag Pulse Torpedo",
    0x08: "Ion Torpedo",
}
BEAM: dict[int, str] = {
    0x00: "None",
    0x01: "Tractor Beam",
    0x02: "Jamming Beam",
    0x03: "Decoy Beam",
    0x04: "Energy Beam",
}
GROUPAI: dict[int, str] = {
    0x00: "Rookie (None)",
    0x01: "Officer",
    0x02: "Veteran",
    0x03: "Ace",
    0x04: "Top Ace",
    0x05: "Jedi (Invincible)",
}
MARKINGS: dict[int, str] = {
    0x00: "Red (TIE - None)",
    0x01: "Gold (TIE - Red)",
    0x02: "Blue (TIE - Gold)",
    0x03: "Green (TIE - Blue)",
}
RADIO: dict[int, str] = {
    0x00: "None",
    0x01: "Team 1 (Imperial)",
    0x02: "Team 2 (Rebel)",
    0x03: "Team 3",
    0x04: "Team 4",
    0x05: "Team 5",
    0x06: "Team 6",
    0x07: "Team 7",
    0x08: "Team 8",
    0x09: "Player 1",
    0x0A: "Player 2",
    0x0B: "Player 3",
    0x0C: "Player 4",
    0x0D: "Player 5",
    0x0E: "Player 6",
    0x0F: "Player 7",
    0x10: "Player 8",
}
FORMATION: dict[int, str] = {
    0x00: "Vic",
    0x01: "Finger Four",
    0x02: "Line Astern",
    0x03: "Line Abreast",
    0x04: "Echelon Right",
    0x05: "Echelon Left",
    0x06: "Double Astern",
    0x07: "Diamond",
    0x08: "Stack",
    0x09: "High X",
    0x0A: "Vic Abreast",
    0x0B: "High Vic",
    0x0C: "Reverse High Vic",
    0x0D: "Reverse Line Astern",
    0x0E: "Stacked Low",
    0x0F: "Abreast Right",
    0x10: "Abreast Left",
    0x11: "Wing Forward",
    0x12: "Wing Back",
    0x13: "Line Astern Up",
    0x14: "Line Astern Down",
    0x15: "Abreast V",
    0x16: "Abreast Inverted V",
    0x17: "Double Astern Mirror",
    0x18: "Double Stacked Astern",
    0x19: "Double Stacked High",
    0x1A: "Diamond 1",
    0x1B: "Diamond 2",
    0x1C: "Flat Pentagon",
    0x1D: "Side Pentagon",
    0x1E: "Front Pentagon",
    0x1F: "Flat Hexagon",
    0x20: "Side Hexagon",
    0x21: "Front Hexagon",
}
ARRIVALDIFFICULTY: dict[int, str] = {
    0x00: "All",
    0x01: "Easy",
    0x02: "Medium",
    0x03: "Hard",
    0x04: "Medium, Hard",
    0x05: "Easy, Medium",
    0x06: "Never",
}
STOPARRIVINGWHEN: dict[int, str] = {
    0x00: "No condition",
    0x01: "Any of this FG completes mission",
    0x02: "Team mission outcome is victory",
    0x03: "Team mission outcome is failure",
}
CONDITION: dict[int, str] = {
    0x00: "Always (true)",
    0x01: "Arrived",
    0x02: "Destroyed",
    0x03: "Attacked",
    0x04: "Captured",
    0x05: "Inspected",
    0x06: "Finished Being Boarded",
    0x07: "Finished Docking",
    0x08: "Disabled",
    0x09: "Survived (exist)",
    0x0A: "None (false)",
    0x0B: "Unused",
    0x0C: "Completed mission",
    0x0D: "Completed Primary Goals",
    0x0E: "Failed Primary Goals",
    0x0F: "Completed Secondary Goals",
    0x10: "Failed Secondary Goals (unused)",
    0x11: "Completed Bonus Goals",
    0x12: "Failed Bonus Goals",
    0x13: "Dropped off",
    0x14: "Reinforced",
    0x15: "0% Shields",
    0x16: "50% Hull",
    0x17: "Out of Warheads",
    0x18: "Cannon system disabled",
    0x19: "be dropped off",
    0x1A: "Destroyed in 1 hit (broken)",
    0x1B: "NOT be disabled",
    0x1C: "NOT be captured",
    0x1D: "Come and go w/o Inspection",
    0x1E: "Begin being boarded",
    0x1F: "NOT being boarded",
    0x20: "begin docking",
    0x21: "NOT begin docking",
    0x22: "50% Shields",
    0x23: "25% Shields",
    0x24: "75% Hull",
    0x25: "25% Hull",
    0x26: "Always Failed",
    0x27: "Inspect/Pickup team modifier",
    0x28: "Unused",
    0x29: "be all Player Craft",
    0x2A: "be all AI Craft",
    0x2B: "come and go",
    0x2C: "be bagged",
    0x2D: "withdraw",
    0x2E: "be carried away",
}
VARIABLETYPE: dict[int, str] = {
    0x00: "None",
    0x01: "Flight Group",
    0x02: "CraftType",
    0x03: "CraftCategory",
    0x04: "ObjectCategory",
    0x05: "IFF",
    0x06: "Order",
    0x07: "CraftWhen",
    0x08: "Global Group",
    0x09: "Adjusted AI skill level",
    0x0A: "Status1",
    0x0B: "All Craft",
    0x0C: "Team",
    0x0D: "Player Slot",
    0x0E: "Before elapsed time",
    0x0F: "All Flight Group except",
    0x10: "All CraftType except",
    0x11: "All CraftCategory except",
    0x12: "All ObjectCategory except",
    0x13: "All IFFs except",
    0x14: "All Global Group except",
    0x15: "All Teams except",
    0x16: "All Player Slot except",
    0x17: "Global Unit",
    0x18: "All Global Unit except",
}
CRAFTCATEGORY: dict[int, str] = {
    0x00: "Starfighters",
    0x01: "Transports",
    0x02: "Freighters/Containers",
    0x03: "Starships",
    0x04: "Utility Craft",
    0x05: "Platforms/Facilities",
    0x06: "Mines",
}
OBJECTCATEGORY: dict[int, str] = {
    0x00: "Craft",
    0x01: "Weapons",
    0x02: "Space Objects",
}
AMOUNT: dict[int, str] = {
    0x00: "100%",
    0x01: "75%",
    0x02: "50%",
    0x03: "25%",
    0x04: "At least one",
    0x05: "All but one",
    0x06: "Special craft",
    0x07: "All non-special craft",
    0x08: "All non-player craft",
    0x09: "Player's craft",
    0x0A: "100% of first wave",
    0x0B: "75% of first wave",
    0x0C: "50% of first wave",
    0x0D: "25% of first wave",
    0x0E: "At least one of first wave",
    0x0F: "All but one of first wave",
    0x10: "66%",
    0x11: "33%",
    0x12: "Each craft",
}
ABORTTRIGGER: dict[int, str] = {
    0x00: "None",
    0x01: "0% Shields",
    0x02: "Cannons disabled",
    0x03: "Out of warheads",
    0x04: "50% Hull",
    0x05: "Attacked",
    0x06: "50% Shields",
    0x07: "25% Shields",
    0x08: "75% Hull",
    0x09: "25% Hull",
}
ORDER: dict[int, str] = {
    0x00: "Hold Steady",
    0x01: "Go Home",
    0x02: "Circle",
    0x03: "Circle and Evade",
    0x04: "Rendezvous",
    0x05: "Disabled",
    0x06: "Await Boarding",
    0x07: "Attack",
    0x08: "Attack Escorts",
    0x09: "Protect",
    0x0A: "Escort",
    0x0B: "Disable",
    0x0C: "Board and Give Cargo",
    0x0D: "Board and Take Cargo",
    0x0E: "Board and Exchange Cargo",
    0x0F: "Board and Capture Cargo",
    0x10: "Board and Destroy Cargo",
    0x11: "Pick up",
    0x12: "Drop off",
    0x13: "Wait",
    0x14: "Wait 2",
    0x15: "SS Patrol Loop",
    0x16: "SS Await Return",
    0x17: "SS Launch",
    0x18: "SS Protect",
    0x19: "SS Protect 2",
    0x1A: "SS Patrol and Attack",
    0x1B: "SS Patrol and Disable",
    0x1C: "Hold Steady 2",
    0x1D: "SS Go Home",
    0x1E: "Hold Steady 3",
    0x1F: "SS Board",
    0x20: "Board to Repair",
    0x21: "Hold Steady 4",
    0x22: "Hold Steady 5",
    0x23: "Hold Steady 6",
    0x24: "Self-destruct",
    0x25: "Kamikaze",
    0x26: "Hold Steady 7",
    0x27: "Null (Hold Steady)",
}
CRAFTWHEN: dict[int, str] = {
    0x00: "Captured",
    0x01: "Inspected",
    0x02: "Finished being boarded",
    0x03: "Finished docking",
    0x04: "Disabled",
    0x05: "Attacked",
    0x06: "Any hull damage",
    0x07: "Special craft",
    0x08: "Non-special craft",
    0x09: "Player's craft",
    0x0A: "Non-player's craft",
    0x0B: "Not attacked (broken)",
    0x0C: "Not disabled",
    0x0D: "Not captured",
    0x0E: "Not inspected (broken)",
    0x0F: "Never (always false)",
    0x10: "Not finished being boarded",
    0x11: "Never",
    0x12: "Not finished docking",
    0x13: "Never",
    0x14: "Never",
    0x15: "Never",
    0x16: "Hull 75% or less",
    0x17: "Hull 50% or less",
    0x18: "Hull 25% or less",
    0x19: "Out of warheads",
}
EVENTTYPE: dict[int, str] = {
    0x03: "Stop",
    0x04: "Title Text",
    0x05: "Caption Text",
    0x06: "Move Map",
    0x07: "Zoom Map",
    0x08: "Clear FG Tags",
    0x09: "FG Tag 1",
    0x0A: "FG Tag 2",
    0x0B: "FG Tag 3",
    0x0C: "FG Tag 4",
    0x0D: "FG Tag 5",
    0x0E: "FG Tag 6",
    0x0F: "FG Tag 7",
    0x10: "FG Tag 8",
    0x11: "Clear Text Tags",
    0x12: "Text Tag 1",
    0x13: "Text Tag 2",
    0x14: "Text Tag 3",
    0x15: "Text Tag 4",
    0x16: "Text Tag 5",
    0x17: "Text Tag 6",
    0x18: "Text Tag 7",
    0x19: "Text Tag 8",
    0x22: "End Briefing",
}


LISTS: dict[str, dict[int, str]] = {
    "PlatformID": PLATFORMID,
    "MissionType": MISSIONTYPE,
    "GoalArgument": GOALARGUMENT,
    "Countermeasures": COUNTERMEASURES,
    "OptionalCraftCategory": OPTIONALCRAFTCATEGORY,
    "DesignationTeam": DESIGNATIONTEAM,
    "CraftType": CRAFTTYPE,
    "Status": STATUS,
    "Warhead": WARHEAD,
    "Beam": BEAM,
    "GroupAI": GROUPAI,
    "Markings": MARKINGS,
    "Radio": RADIO,
    "Formation": FORMATION,
    "ArrivalDifficulty": ARRIVALDIFFICULTY,
    "StopArrivingWhen": STOPARRIVINGWHEN,
    "Condition": CONDITION,
    "VariableType": VARIABLETYPE,
    "CraftCategory": CRAFTCATEGORY,
    "ObjectCategory": OBJECTCATEGORY,
    "Amount": AMOUNT,
    "AbortTrigger": ABORTTRIGGER,
    "Order": ORDER,
    "CraftWhen": CRAFTWHEN,
    "EventType": EVENTTYPE,
}

# Roles store only the first three letters of a Designation name.
DESIGNATION_BY_CODE: dict[str, str] = {
    name[:3].upper(): name for name in DESIGNATION.values()
}


def name_of(list_name: str, value: int) -> str | None:
    """Return the document's name for ``value`` in list ``list_name``.

    Returns ``None`` when the list does not name that value. Raises
    ``KeyError`` for a list name not in ``LISTS``. Does not check that the
    value is in range for the field it came from.
    """
    return LISTS[list_name].get(value)


def designation_name(code: str) -> str | None:
    """Return the Designation name for a role's three-letter code.

    The match ignores letter case (stock missions store both "PRI" and
    "pri"). Returns ``None`` when no Designation starts with those letters.
    Does not check the role's team character.
    """
    return DESIGNATION_BY_CODE.get(code.upper())


def designation_team_name(code: int) -> str | None:
    """Return the DesignationTeam name for a role's stored team character.

    The match ignores letter case (stock missions store "a" and "h" as well
    as "A" and "H"). Returns ``None`` for a code the list does not name.
    Does not check the role's designation letters.
    """
    if code in DESIGNATIONTEAM:
        return DESIGNATIONTEAM[code]
    return DESIGNATIONTEAM.get(ord(chr(code).upper())) if 0 <= code < 256 else None
