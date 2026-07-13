// Living Settlements -- pocs/Poc024_JobHook.cpp
// READ-ONLY em efeito: cada detour LOGA os argumentos e chama o original
// (sem mudar comportamento). ASCII-only. So simbolos verificados (sec.0.2).
// Objetivo: quando o jogador atribui uma mina pela UI, ver EXATAMENTE qual
// funcao/task/subject/flags o jogo usa -> replicar com certeza (fim do palpite).
// Padrao de hook de metodo copiado do RE_Kenshi (Bugs.cpp: LoadingWindow::hide):
//   AddHook(GetRealAddress(&Class::method), detour, &orig); detour tem `this`
//   como 1o parametro (ABI x64: rcx=this). So funcoes NAO-virtuais (estas sao).
#include "pocs/Poc024_JobHook.h"
#include "core/LsConfig.h"
#include "core/Diagnostics.h"

#include <core/Functions.h>   // KenshiLib::AddHook / GetRealAddress / HookStatus
#include <kenshi/util/OgreUnordered.h> // ogre_unordered_set (PlayerInterface.h usa, nao define)
#include <kenshi/PlayerInterface.h>
#include <kenshi/RootObject.h> // getInstanceID (virtual -- seguro em qualquer RootObject)
#include <kenshi/InstanceID.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Enums.h>      // TaskType
#include <kenshi/util/hand.h>
#include <ogre/OgreVector3.h>

#include <string>
#include <sstream>

namespace ls {
namespace pocs {

namespace {

// Trampolins originais (preenchidos por AddHook).
void (*g_addJobSelOrig)(PlayerInterface*, TaskType, RootObject*, bool, bool,
                        const Ogre::Vector3&) = 0;
void (*g_newTaskOrig)(PlayerInterface*, TaskType, const hand&, Building*,
                      const Ogre::Vector3&, bool) = 0;

std::string uidOf(RootObject* o) {
    if (o == 0) {
        return std::string();
    }
    InstanceID* iid = o->getInstanceID();   // virtual: resolve p/ o tipo real
    return (iid != 0) ? iid->uid : std::string();
}

// Detour de PlayerInterface::addJobSelectedCharacters(task, subject, shift, add, loc).
void addJobSelHook(PlayerInterface* self, TaskType task, RootObject* subject,
                   bool shift, bool add, const Ogre::Vector3& loc) {
    std::string uid = uidOf(subject);
    std::ostringstream s;
    s << "JOBHOOK addJobSelectedCharacters: task=" << static_cast<int>(task)
      << " subject=" << (uid.empty() ? std::string("(sem-uid)") : uid)
      << " shift=" << (shift ? 1 : 0) << " add=" << (add ? 1 : 0)
      << " pos=(" << loc.x << "," << loc.y << "," << loc.z << ")";
    diag::log(s.str());
    diag::flush();
    g_addJobSelOrig(self, task, subject, shift, add, loc);
}

// Detour de newPlayerTaskSelectedCharacters(t, targetH, destinationIndoors, clickpos, addDontClear).
void newTaskHook(PlayerInterface* self, TaskType t, const hand& targetH,
                 Building* dest, const Ogre::Vector3& clickpos, bool addDontClear) {
    std::string uid;
    hand th = targetH;
    if (th.isValid()) {
        Building* b = th.getBuilding();
        uid = uidOf(b);
    }
    std::ostringstream s;
    s << "JOBHOOK newPlayerTaskSelectedCharacters: task=" << static_cast<int>(t)
      << " targetH=" << (uid.empty() ? std::string("(sem-uid/nao-predio)") : uid)
      << " dest=" << uidOf(dest) << " addDontClear=" << (addDontClear ? 1 : 0)
      << " pos=(" << clickpos.x << "," << clickpos.y << "," << clickpos.z << ")";
    diag::log(s.str());
    diag::flush();
    g_newTaskOrig(self, t, targetH, dest, clickpos, addDontClear);
}

} // namespace

bool installJobHooks() {
    if (!LS_ENABLE_JOBHOOK) {
        return false;
    }
    bool ok = true;
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&PlayerInterface::addJobSelectedCharacters),
            reinterpret_cast<void*>(&addJobSelHook),
            reinterpret_cast<void**>(&g_addJobSelOrig))) {
        diag::error("JOBHOOK: falha ao hookar addJobSelectedCharacters");
        ok = false;
    }
    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&PlayerInterface::newPlayerTaskSelectedCharacters),
            reinterpret_cast<void*>(&newTaskHook),
            reinterpret_cast<void**>(&g_newTaskOrig))) {
        diag::error("JOBHOOK: falha ao hookar newPlayerTaskSelectedCharacters");
        ok = false;
    }
    if (ok) {
        diag::milestone("JOBHOOK instalado: atribua uma mina pela UI e veremos a "
                        "chamada exata (funcao/task/subject/flags)");
    }
    return ok;
}

} // namespace pocs
} // namespace ls
