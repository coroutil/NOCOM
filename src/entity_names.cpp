#include "entity_names.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace {
struct NameAlias {
    const char* needle;
    const char* label;
};

static const NameAlias kAliases[] = {

    {"npc_combine_helicopter_ball", "Combine Helicopter Ball"},
    {"npc_combinehelicopterball", "Combine Helicopter Ball"},
    {"npc_helicopter_ball", "Combine Helicopter Ball"},
    {"npc_helicopterball", "Combine Helicopter Ball"},
    {"combine_helicopter_ball", "Combine Helicopter Ball"},
    {"combinehelicopterball", "Combine Helicopter Ball"},
    {"models/combine_helicopter_ball", "Combine Helicopter Ball"},
    {"helicopter_ball", "Combine Helicopter Ball"},
    {"helicopterball", "Combine Helicopter Ball"},
    {"npc_combine_helicopter", "Combine Helicopter"},
    {"combine_helicopter", "Combine Helicopter"},
    {"combinehelicopter", "Combine Helicopter"},
    {"npc_helicopter", "Combine Helicopter"},
    {"npc_turret_floor", "Floor Turret"},
    {"npc_turret_ceiling", "Ceiling Turret"},
    {"npc_turret_ground", "Ground Turret"},
    {"npc_combinegunship", "Combine Gunship"},
    {"combinegunship", "Combine Gunship"},
    {"npc_combinedropship", "Combine Dropship"},
    {"combinedropship", "Combine Dropship"},
    {"npc_antlionguardian", "Antlion Guardian"},
    {"antlionguardian", "Antlion Guardian"},
    {"npc_antlionguard", "Antlion Guard"},
    {"antlionguard", "Antlion Guard"},
    {"antlion_guard", "Antlion Guard"},
    {"npc_antlion_worker", "Antlion Worker"},
    {"antlion_worker", "Antlion Worker"},
    {"npc_antlion_grub", "Antlion Grub"},
    {"antlion_grub", "Antlion Grub"},
    {"npc_poisonzombie", "Poison Zombie"},
    {"poisonzombie", "Poison Zombie"},
    {"npc_fastzombie", "Fast Zombie"},
    {"fastzombie", "Fast Zombie"},
    {"npc_zombine", "Zombine"},
    {"zombine", "Zombine"},
    {"npc_fastzombie_torso", "Fast Zombie Torso"},
    {"fastzombie_torso", "Fast Zombie Torso"},
    {"npc_zombie_torso", "Zombie Torso"},
    {"npc_zombie_soldier", "Zombie Soldier"},
    {"npc_zombie_barney", "Zombie Barney"},
    {"zombie_torso", "Zombie Torso"},
    {"npc_fastheadcrab", "Fast Headcrab"},
    {"fastheadcrab", "Fast Headcrab"},
    {"headcrab_fast", "Fast Headcrab"},
    {"npc_poisonheadcrab", "Poison Headcrab"},
    {"poisonheadcrab", "Poison Headcrab"},
    {"headcrab_black", "Poison Headcrab"},
    {"headcrab_poison", "Poison Headcrab"},
    {"npc_hunter", "Hunter"},
    {"hunter_flechette", "Hunter Flechette"},
    {"models/hunter", "Hunter"},
    {"hunter", "Hunter"},
    {"hunterflechette", "Hunter Flechette"},
    {"npc_combine_camera", "Combine Camera"},
    {"combine_camera", "Combine Camera"},
    {"npc_combine_elite", "Combine Elite"},
    {"combine_elite", "Combine Elite"},
    {"npc_combine_s_elite", "Combine Elite"},
    {"npc_combine_s_shotgun", "Combine Shotgunner"},
    {"npc_combine_s_prisonguard", "Combine Prison Guard"},
    {"npc_combine_s_friendly", "Friendly Combine Soldier"},
    {"npc_combine_s_enemy", "Combine Soldier"},
    {"npc_metropolice", "Civil Protection Officer"},
    {"metropolice", "Civil Protection Officer"},
    {"npc_manhack", "Manhack"},
    {"npc_clawscanner", "Claw Scanner"},
    {"clawscanner", "Claw Scanner"},
    {"npc_cscanner", "City Scanner"},
    {"npc_combine_s", "Combine Soldier"},
    {"npc_advisor", "Combine Advisor"},
    {"models/advisor", "Combine Advisor"},
    {"npc_shieldscanner", "Shield Scanner"},
    {"npc_stalker", "Stalker"},
    {"npc_strider", "Strider"},
    {"npc_rollermine", "Rollermine"},
    {"npc_barnacle", "Barnacle"},
    {"npc_ichthyosaur", "Ichthyosaur"},
    {"npc_bullsquid", "Bullsquid"},
    {"npc_houndeye", "Houndeye"},
    {"npc_headcrab", "Headcrab"},
    {"headcrab", "Headcrab"},
    {"npc_antlion", "Antlion"},
    {"antlion", "Antlion"},
    {"npc_zombie", "Zombie"},
    {"zombie", "Zombie"},
    {"prop_combine_ball", "Combine Energy Ball"},
    {"npc_combineball", "Combine Energy Ball"},
    {"combineball", "Combine Energy Ball"},
    {"combine_ball", "Combine Energy Ball"},
    {"npc_grenade_frag", "Frag Grenade"},
    {"grenade_frag", "Frag Grenade"},
    {"grenade_bugbait", "Bugbait"},
    {"grenade_helicopter", "Hunter-Chopper Grenade"},
    {"grenade_homer", "Homing Grenade"},
    {"grenade_spit", "Antlion Spit"},
    {"combine_mine", "Combine Mine"},
    {"prop_ragdoll", "Ragdoll"},
    {"grenade_ar2", "AR2 Energy Ball"},
    {"ar2_altfire", "AR2 Energy Ball"},
    {"rpg_missile", "RPG Missile"},
    {"rpgmissile", "RPG Missile"},
    {"crossbow_bolt", "Crossbow Bolt"},

    {"weapon_crowbar", "Crowbar"}, {"cweaponcrowbar", "Crowbar"}, {"w_crowbar", "Crowbar"},
    {"weapon_stunstick", "Stunstick"}, {"weapon_stunbaton", "Stunstick"},
    {"cweaponstunstick", "Stunstick"}, {"w_stunbaton", "Stunstick"}, {"w_stunstick", "Stunstick"},
    {"weapon_revolver", "Revolver"}, {"cweaponrevolver", "Revolver"}, {"w_revolver", "Revolver"},
    {"weapon_pistol", "Pistol"}, {"cweaponpistol", "Pistol"}, {"w_pistol", "Pistol"},
    {"weapon_smg1", "SMG"}, {"cweaponsmg1", "SMG"}, {"w_smg1", "SMG"}, {"w_smg", "SMG"},
    {"weapon_ar2", "Pulse Rifle"}, {"weapon_irifle", "Pulse Rifle"}, {"cweaponar2", "Pulse Rifle"},
    {"w_irifle", "Pulse Rifle"}, {"w_ar2", "Pulse Rifle"},
    {"weapon_shotgun", "Shotgun"}, {"cweaponshotgun", "Shotgun"}, {"w_shotgun", "Shotgun"},
    {"weapon_crossbow", "Crossbow"}, {"cweaponcrossbow", "Crossbow"}, {"w_crossbow", "Crossbow"},
    {"weapon_rpg", "RPG"}, {"cweaponrpg", "RPG"}, {"w_rpg", "RPG"}, {"w_rocket_launcher", "RPG"},
    {"weapon_slam", "SLAM"}, {"cweaponslam", "SLAM"}, {"w_slam", "SLAM"},
    {"weapon_physcannon", "Gravity Gun"}, {"weapon_gravitygun", "Gravity Gun"},
    {"cweaponphyscannon", "Gravity Gun"}, {"w_physcannon", "Gravity Gun"},
    {"w_physics", "Gravity Gun"}, {"w_gravitygun", "Gravity Gun"},
    {"item_item_crate", "Item Crate"},
    {"item_healthkit", "Health Kit"},
    {"item_healthvial", "Health Vial"},
    {"item_health_vial", "Health Vial"},
    {"citem_healthvial", "Health Vial"},
    {"c_item_healthvial", "Health Vial"},
    {"health_vial", "Health Vial"},
    {"healthvial", "Health Vial"},
    {"item_battery", "Suit Battery"},
    {"item_ammo", "Ammo Pickup"},
    {"npc_alyx", "Alyx Vance"},
    {"models/alyx", "Alyx Vance"},
    {"alyx", "Alyx Vance"},
    {"npc_barney", "Barney Calhoun"},
    {"models/barney", "Barney Calhoun"},
    {"barney", "Barney Calhoun"},
    {"npc_kleiner", "Dr. Isaac Kleiner"},
    {"models/kleiner", "Dr. Isaac Kleiner"},
    {"kleiner", "Dr. Isaac Kleiner"},
    {"npc_eli", "Dr. Eli Vance"},
    {"cnpc_eli", "Dr. Eli Vance"},
    {"models/eli", "Dr. Eli Vance"},
    {"eli_vance", "Dr. Eli Vance"},
    {"male_eli", "Dr. Eli Vance"},
    {"npc_mossman", "Dr. Judith Mossman"},
    {"models/mossman", "Dr. Judith Mossman"},
    {"mossman", "Dr. Judith Mossman"},
    {"npc_monk", "Father Grigori"},
    {"grigori", "Father Grigori"},
    {"monk", "Father Grigori"},
    {"npc_dog", "Dog"},
    {"models/dog", "Dog"},
    {"dog.mdl", "Dog"},
    {"npc_vortigaunt", "Vortigaunt"},
    {"vortigaunt", "Vortigaunt"},
    {"npc_breen", "Dr. Wallace Breen"},
    {"npc_alyx_scientist", "Alyx Vance"},
    {"breen", "Dr. Wallace Breen"},
        {"npc_odessa", "Odessa Cubbage"},
    {"odessa", "Odessa Cubbage"},
    {"npc_magnusson", "Dr. Arne Magnusson"},
    {"magnusson", "Dr. Arne Magnusson"},
    {"npc_gman", "G-Man"},
    {"gman", "G-Man"},
    {"g-man", "G-Man"},
    {"npc_citizen_medic", "Citizen Medic"},
    {"npc_citizen_rebel", "Rebel Citizen"},
    {"npc_citizen", "Citizen"},
    {"npc_refugee", "Refugee"},
    {"npc_rebel", "Rebel"},
    {"citizen", "Citizen"},
    {"refugee", "Refugee"},
    {"rebel", "Rebel"},
    {"npc_medic", "Medic"},
    {"medic", "Medic"},
    {"npc_engineer", "Engineer"},
    {"engineer", "Engineer"},
    {"npc_crow", "Crow"},
    {"crow.mdl", "Crow"},
    {"npc_seagull", "Seagull"},
    {"seagull", "Seagull"},
    {"npc_pigeon", "Pigeon"},
    {"pigeon", "Pigeon"},
    {"npc_cockroach", "Cockroach"},
    {"cockroach", "Cockroach"},
    {"npc_bullseye", "Bullseye"},
    {"npc_assassin", "Assassin"},
    {"monster_human_grunt", "Human Grunt"},
    {"monster_alien_grunt", "Alien Grunt"},
    {"monster_houndeye", "Houndeye"},
    {"monster_barnacle", "Barnacle"},
    {"monster_zombie", "Zombie"},
    {"monster_headcrab", "Headcrab"},
    {"monster_alien_slave", "Vortigaunt"},
    {"monster_gargantua", "Gargantua"},
    {"monster_tentacle", "Tentacle"},
    {"monster_ichthyosaur", "Ichthyosaur"},
    {"monster_bullsquid", "Bullsquid"},
    {"monster_cockroach", "Cockroach"},
    {"monster_leech", "Leech"},
    {"npc_fisherman", "Fisherman"},
    {"fisherman", "Fisherman"},
    {"npc_security", "Security Guard"},
    {"npc_vehicledriver", "Vehicle Driver"},
    {"npc_turret", "Turret"},
    {"npc_helicopter", "Hunter-Chopper"},
    {"helicopter.mdl", "Hunter-Chopper"},
    {"npc_combine", "Combine Soldier"},
    {"combine_soldier", "Combine Soldier"},
    {"floor_turret", "Floor Turret"},
    {"ceiling_turret", "Ceiling Turret"},
    {"ground_turret", "Ground Turret"},
    {"npc_launcher", "Launcher"},
    {"npc_sniper", "Combine Sniper"},
};

static bool contains(const char* haystack, const char* needle) {
    return haystack && needle && *needle && std::strstr(haystack, needle) != nullptr;
}

static void lowerCopy(char* dst, size_t cap, const char* src) {
    if (!cap) return;
    size_t j = 0;
    if (src) {
        for (; src[j] && j + 1 < cap; ++j) {
            unsigned char c = static_cast<unsigned char>(src[j]);
            dst[j] = static_cast<char>(std::tolower(c));
        }
    }
    dst[j] = 0;
}

static void copyText(char* out, size_t cap, const char* text) {
    if (!out || cap == 0) return;
    if (!text) text = "";
    std::snprintf(out, cap, "%s", text);
    out[cap - 1] = 0;
}

static void formatFallback(const char* raw, char* out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!raw || !*raw) return;

    const char* base = raw;
    const char* slash = std::strrchr(raw, '/');
    const char* backslash = std::strrchr(raw, '\\');
    if (slash && (!backslash || slash > backslash)) base = slash + 1;
    else if (backslash) base = backslash + 1;

    char cleaned[128]{};
    copyText(cleaned, sizeof(cleaned), base);
    char* dot = std::strrchr(cleaned, '.');
    if (dot && _stricmp(dot, ".mdl") == 0) *dot = 0;

    char* p = cleaned;
    if (_strnicmp(p, "C_NPC_", 6) == 0) p += 6;
    else if (_strnicmp(p, "CNPC_", 5) == 0) p += 5;
    else if (_strnicmp(p, "npc_", 4) == 0) p += 4;
    else if (_strnicmp(p, "C_", 2) == 0) p += 2;
    else if (p[0] == 'C' && p[1] && std::isupper(static_cast<unsigned char>(p[1]))) ++p;

    size_t j = 0;
    bool pendingSpace = false;
    for (; *p && j + 1 < cap; ++p) {
        unsigned char uc = static_cast<unsigned char>(*p);
        if (*p == '_' || *p == '-' || *p == '/') {
            pendingSpace = (j > 0);
            continue;
        }
        if (std::isspace(uc)) {
            pendingSpace = (j > 0);
            continue;
        }
        if (pendingSpace && j + 1 < cap) out[j++] = ' ';
        pendingSpace = false;
        if (std::isupper(uc) && j > 0 && out[j - 1] != ' ' &&
            std::islower(static_cast<unsigned char>(out[j - 1])) && j + 1 < cap)
            out[j++] = ' ';
        out[j++] = static_cast<char>(uc);
    }
    out[j] = 0;

    bool startWord = true;
    for (size_t i = 0; out[i]; ++i) {
        unsigned char c = static_cast<unsigned char>(out[i]);
        if (out[i] == ' ') { startWord = true; continue; }
        out[i] = startWord ? static_cast<char>(std::toupper(c)) : static_cast<char>(std::tolower(c));
        startWord = false;
    }
    if (!out[0]) copyText(out, cap, raw);
}
}

void beautifyEntityName(const char* networkClass, const char* modelPath,
                        char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = 0;

    char combined[512]{};
    char lowerClass[192]{};
    char lowerModel[320]{};
    lowerCopy(lowerClass, sizeof(lowerClass), networkClass);
    lowerCopy(lowerModel, sizeof(lowerModel), modelPath);
    std::snprintf(combined, sizeof(combined), "%s %s", lowerClass, lowerModel);
    combined[sizeof(combined) - 1] = 0;

    for (const NameAlias& alias : kAliases) {
        if (contains(combined, alias.needle)) {
            copyText(out, outSize, alias.label);
            return;
        }
    }

    if (modelPath && modelPath[0]) formatFallback(modelPath, out, outSize);
    if (!out[0] && networkClass && networkClass[0])
        formatFallback(networkClass, out, outSize);
    if (!out[0]) copyText(out, outSize, "Unknown Entity");
}
