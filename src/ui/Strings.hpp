// SPDX-License-Identifier: MIT
#pragma once

// Editor texts. English is the default; French is a per-user option (see Settings).
// Control labels stay the same in both languages: they use the usual vocal-mixing
// vocabulary.

#include "engine/Parameters.hpp"

#include <array>
#include <cstddef>

namespace titv::ui {

enum class Language { English, French };

enum class Text { TempoFallback, TempoHost, SettingsHelp, AutoLevelHelp, AutoLevelUndoHelp, PresetHelp,
                  UserPresetsHeader, SavePreset, PresetNamePlaceholder, SavePresetHelp, SavePresetReplaceHelp,
                  DeletePresetConfirm, DeletePresetHelp, AboutHelp, AboutBy, AboutFree, AboutLicence,
                  AboutBuiltWith, AboutClose, SettingsTitle, SettingsLanguage, SettingsClose, UpdateAvailable, UpdateHelp, UpdateDismissHelp,
                  SettingsUpdates, Count };

inline constexpr std::array<const char*, kParamCount> kHelpFrench = { {
    "Bypass : sortie directe du signal d'origine, sans traitement.", // global_bypass
    "HPF : coupe-bas qui retire le grondement sous la voix, en entrée et en fin de chaîne.", // hpf_enabled
    "Input : gain d'entrée, à régler pour atteindre la zone cible du vumètre.", // input_gain_db
    "Output : niveau de sortie global, après le mélange des effets.", // output_gain_db
    "COMPRESS : compression parallèle et série, puis limiteur, pour tenir la voix devant.", // compress_amount
    "TONE : égaliseur de la voix.", // tone_enabled
    "Body : plateau grave, pour le corps de la voix.", // tone_body_db
    "Mid : cloche dans le médium.", // tone_mid_db
    "Presence : cloche haut-médium, pour l'intelligibilité.", // tone_presence_db
    "Air : aigu dynamique, qui recule sur les sifflantes.", // tone_air_db
    "COLOR : effets de couleur.", // color_enabled
    "De-Ess : dé-esseur, réduit les sifflantes.", // color_deess
    "Saturate : saturation harmonique en parallèle.", // color_saturate
    "Radio : filtre téléphone.", // color_radio
    "Double : doublage stéréo par copies légèrement décalées.", // color_double
    "Chorus : chorus par retards modulés.", // color_chorus
    "ECHO : delay synchronisé au tempo.", // echo_enabled
    "Send : envoi vers le delay ; la queue continue quand on le baisse.", // echo_send
    "Repeats : feedback du delay, de une répétition à une queue quasi infinie.", // echo_repeats
    "Lo-Fi : filtre téléphone sur les seules répétitions.", // echo_lofi
    "Note : division rythmique du delay (T = triolet, D = pointée).", // echo_note
    "Bounce : delay ping-pong, répétitions alternées gauche/droite.", // echo_bounce
    "SPACE : réverbes.", // space_enabled
    "Room : active la réverbe de pièce.", // space_room_enabled
    "Plate : active la réverbe à plaque.", // space_plate_enabled
    "Hall : active la réverbe de salle.", // space_hall_enabled
    "Ambient : active la réverbe d'ambiance longue.", // space_ambient_enabled
    "Room : envoi vers la réverbe de pièce.", // space_room
    "Plate : envoi vers la réverbe à plaque.", // space_plate
    "Hall : envoi vers la réverbe de salle.", // space_hall
    "Ambient : envoi vers la réverbe d'ambiance longue.", // space_ambient
    "Voice : adapte HPF, TONE, De-Ess, Saturate et Radio à une voix d'homme, neutre ou de femme.", // voice_register
} };

inline constexpr std::array<const char*, static_cast<std::size_t>(Text::Count)> kTextsEnglish = { {
    "%.0f BPM (default)",
    "%.1f BPM",
    "Settings: editor language.",
    "Auto Level: listens to 10 s of voice, then sets Input so the meter sits in the target zone. Click again to cancel.",
    "Undo Auto Level: restores the previous Input gain.",
    "Presets: factory starting points and your own. Input, Voice and Bypass stay as they are; * marks a modified preset.",
    "USER",
    "+ Save current settings\u2026",
    "Preset name",
    "Type a name, then Enter (or \u2713) to save, Esc to cancel.",
    "A preset with this name exists: Enter replaces it.",
    "Delete?",
    "Delete this user preset (click again to confirm).",
    "About: credits and licence.",
    "Created by Pierre-Alain David",
    "Free and open-source software, at no cost.",
    "Released under the MIT License: you may use, study, share and modify it. It comes with no "
    "warranty.",
    "Built with DPF (ISC, Filipe Coelho), CLAP (MIT), LV2 (ISC), pugl (ISC), NanoVG (zlib) and the DejaVu Sans "
    "font.",
    "Click anywhere to close.",
    "SETTINGS",
    "Language",
    "Click outside the panel or press Esc to close.",
    "Update available: %s",
    "A newer version is available: click to open its download page.",
    "Ignore this version: the next one will be announced again.",
    "Check for updates",
} };

inline constexpr std::array<const char*, static_cast<std::size_t>(Text::Count)> kTextsFrench = { {
    "%.0f BPM (par défaut)",
    "%.1f BPM",
    "Réglages : langue de l'éditeur.",
    "Auto Level : écoute 10 s de voix, puis règle Input pour placer le vumètre dans la zone cible. Un nouveau clic annule.",
    "Annuler Auto Level : rétablit le gain Input précédent.",
    "Presets : points de départ d'usine et les vôtres. Input, Voice et Bypass restent tels quels ; * signale un preset modifié.",
    "UTILISATEUR",
    "+ Enregistrer les réglages\u2026",
    "Nom du preset",
    "Saisissez un nom, puis Entrée (ou \u2713) pour enregistrer, Échap pour annuler.",
    "Un preset porte déjà ce nom : Entrée le remplace.",
    "Supprimer ?",
    "Supprimer ce preset utilisateur (cliquer à nouveau pour confirmer).",
    "À propos : crédits et licence.",
    "Créé par Pierre-Alain David",
    "Logiciel libre et open source, entièrement gratuit.",
    "Distribué sous licence MIT : vous pouvez l'utiliser, l'étudier, le partager et le modifier. Il est "
    "fourni sans aucune garantie.",
    "Construit avec DPF (ISC, Filipe Coelho), CLAP (MIT), LV2 (ISC), pugl (ISC), NanoVG (zlib) et la police "
    "DejaVu Sans.",
    "Cliquer n'importe où pour fermer.",
    "RÉGLAGES",
    "Langue",
    "Cliquer hors du panneau ou Échap pour fermer.",
    "Mise à jour disponible : %s",
    "Une version plus récente est disponible : cliquer pour ouvrir sa page de téléchargement.",
    "Ignorer cette version : la suivante sera de nouveau signalée.",
    "Rechercher les mises à jour",
} };

inline const char* helpText(Param p, Language lang)
{
    return lang == Language::French ? kHelpFrench[index(p)] : info(p).help;
}

inline const char* text(Text t, Language lang)
{
    return (lang == Language::French ? kTextsFrench : kTextsEnglish)[static_cast<std::size_t>(t)];
}

} // namespace titv::ui
