// Living Settlements — pocs/Poc002_EnumerarBase.cpp
#include "pocs/Poc002_EnumerarBase.h"
#include "core/Diagnostics.h"
#include "core/LsConfig.h"

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/Town.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Enums.h>
#include <kenshi/util/lektor.h>
#include <ogre/OgreVector3.h>

#include <cstdint>
#include <sstream>
#include <map>
#include <cstdlib> // free() — cleanup do buffer do lektor (padrao oficial)

namespace ls {
namespace pocs {

namespace {

// Enums.h não oferece conversão para string (confirmado no recon) —
// tabela local, apenas para diagnóstico legível. Os ORDINAIS vêm do
// enum BuildingFunction (Enums.h:123-156); manter em sincronia.
const char* buildingFunctionName(int f) {
    static const char* const NAMES[] = {
        "ANY", "MINE", "RESOURCE_STORAGE", "RESEARCH", "REFINERY",
        "GENERATOR", "BED", "TRAINING", "CAGE", "SHOP", "CRAFTING",
        "CORPSE_DISPOSAL", "TURRET", "GENERAL_STORAGE", "ITEM_FURNACE",
        "LIGHT", "TABLE", "CHAIR", "FLUFF", "SHELL_WITH_INTERIOR",
        "WALL", "GATE", "DOOR", "BATTERY", "THRONE", "SKELETON_BED",
        "RAIN_COLLECTOR", "MINE_NATURAL", "STEERING", "ENGINE",
        "LIQUID_TANK"
    };
    static const int COUNT = static_cast<int>(sizeof(NAMES) / sizeof(NAMES[0]));
    if (f >= 0 && f < COUNT)
        return NAMES[f];
    return "?";
}

// TownAlarmState (Town.h:11-17).
const char* alarmName(int a) {
    static const char* const NAMES[] = {
        "NONE", "INTRUDER", "ESCAPE", "ATTACK"
    };
    if (a >= 0 && a < 4)
        return NAMES[a];
    return "?";
}

Character* firstValidPlayerCharacter(GameWorld* world) {
    if (world == 0 || world->player == 0)
        return 0;
    lektor<Character*>& chars = world->player->playerCharacters;
    for (uint32_t i = 0; i < chars.size(); ++i) {
        if (chars[i] != 0)
            return chars[i];
    }
    return 0;
}

} // namespace

void poc002Run(GameWorld* world) {
    Character* anchor = firstValidPlayerCharacter(world);
    if (anchor == 0) {
        diag::log("POC-002: nenhum personagem do jogador disponivel");
        return;
    }

    // Assentamento corrente (FACT-012: TownBase é o assentamento nativo;
    // base do jogador = TOWN_OUTPOST).
    TownBase* town = anchor->getCurrentTownLocation();
    if (town != 0) {
        std::ostringstream tline;
        tline << "POC-002: assentamento \"" << town->getKnownName() << "\""
              << (town->isOutpost() ? " (outpost do jogador)" : "")
              << " alarme=" << alarmName(static_cast<int>(town->getAlarmState()));
        diag::log(tline.str());
    } else {
        diag::log("POC-002: personagem fora de assentamento (town nulo)");
    }

    // Enumeração espacial de edifícios ao redor do personagem-âncora.
    // itemType::BUILDING = 0 (Enums.h:6, primeiro valor do enum).
    //
    // CONTRATO SOB TESTE (é exatamente o unknown que esta POC valida):
    // passamos um lektor default-construído {count=0,maxSize=0,stuff=null}
    // e esperamos que o jogo o preencha. Evidência ANÁLOGA (não desta
    // função): o exemplo oficial Dialogue.cpp:156-158 passa um lektor
    // vazio para ActivePlatoon::getCharactersInArea e depois libera o
    // buffer com free(). Nenhum exemplo oficial chama
    // getObjectsWithinSphere — por isso logamos valid()/capacity()
    // abaixo, para provar (ou refutar) o contrato no gate (doc §0.2).
    lektor<RootObject*> results;
    // Centro FIXO da base (achado do 1o run: ancorar em playerCharacters[0]
    // -- personagem MOVEL -- fazia a janela seguir a unidade ate a linha de
    // frente e NUNCA ver o perimetro; a contagem oscilava 24->0->6). O
    // TownBase e o centro estavel do outpost; getRadius() cobre o perimetro
    // inteiro em vez de 64m ao redor de um personagem que se desloca.
    Ogre::Vector3 center = anchor->getPosition();
    float radius = LS_POC002_RADIUS;
    if (town != 0) {
        center = town->getPosition();
        float tr = town->getRadius();
        if (tr > radius)
            radius = tr;
    }
    {
        std::ostringstream cl;
        cl << "POC-002: varredura centrada em "
           << (town != 0 ? "TownBase" : "personagem[0]")
           << " (" << center.x << "," << center.y << "," << center.z
           << ") raio=" << radius << "m";
        diag::log(cl.str());
    }
    world->getObjectsWithinSphere(results, center, radius,
                                  BUILDING, LS_POC002_MAX_RESULTS, 0);

    {
        std::ostringstream probe;
        probe << "POC-002: contrato lektor pos-chamada: valid="
              << (results.valid() ? 1 : 0)
              << " count=" << results.size()
              << " capacity=" << results.capacity();
        diag::log(probe.str());
    }

    std::map<int, int> countByFunction;
    int total = 0;
    int damaged = 0;
    int detailBudget = 24; // orçamento de linhas de detalhe (RISK-004)

    for (uint32_t i = 0; i < results.size(); ++i) {
        RootObject* obj = results[i];
        if (obj == 0)
            continue;
        ++total;

        // getDataType confirmou BUILDING pelo filtro da consulta; o
        // downcast é seguro dentro do contrato da API.
        Building* b = static_cast<Building*>(obj);

        int fn = static_cast<int>(b->getSpecialFunction());
        countByFunction[fn] += 1;

        bool isDam = b->isDamaged();
        if (isDam)
            ++damaged;

        if (detailBudget > 0) {
            --detailBudget;
            std::ostringstream bline;
            bline << "  edificio \"" << b->getName() << "\""
                  << " fn=" << buildingFunctionName(fn)
                  << (isDam ? " DANIFICADO" : "");
            TownBase* bt = b->getTown();
            if (bt != 0 && bt != town)
                bline << " town=\"" << bt->getKnownName() << "\"";
            diag::log(bline.str());
        }
    }

    std::ostringstream sum;
    sum << "POC-002: " << total << " edificio(s) em raio "
        << radius << "m; " << damaged << " danificado(s)";
    diag::log(sum.str());

    for (std::map<int, int>::const_iterator it = countByFunction.begin();
         it != countByFunction.end(); ++it) {
        std::ostringstream fline;
        fline << "  fn " << buildingFunctionName(it->first)
              << " x" << it->second;
        diag::log(fline.str());
    }

    // Cleanup do buffer alocado pelo jogo: lektor NAO tem destrutor
    // (lektor.h) — sem este free o buffer vaza a cada rodada. Padrão
    // idêntico ao exemplo oficial (Dialogue.cpp: "// cleanup /
    // if (characters.stuff) free(characters.stuff);").
    if (results.stuff != 0) {
        free(results.stuff);
        results.stuff = 0;
        results.count = 0;
        results.maxSize = 0;
    }
}

} // namespace pocs
} // namespace ls
