#include "RoguelikeCustomStartTaskPlugin.h"

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Status.h"
#include "Task/ProcessTask.h"
#include "Utils/Logger.hpp"
#include "Vision/OCRer.h"

bool asst::RoguelikeCustomStartTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    if (!RoguelikeConfig::is_valid_theme(m_config->get_theme())) {
        Log.error("Roguelike name doesn't exist!");
        return false;
    }
    const std::string roguelike_name = m_config->get_theme() + "@";
    const std::string& task = details.get("details", "task", "");
    std::string_view task_view = task;
    if (task_view.starts_with(roguelike_name)) {
        task_view.remove_prefix(roguelike_name.length());
    }
    static const std::unordered_map<std::string_view, std::pair<AsstMsg, RoguelikeCustomType>> TaskMap = {
        { "Roguelike@SquadDefault", { AsstMsg::SubTaskCompleted, RoguelikeCustomType::Squad } },
        { "Roguelike@RolesDefault", { AsstMsg::SubTaskCompleted, RoguelikeCustomType::Roles } },
        { "Roguelike@RecruitMain", { AsstMsg::SubTaskStart, RoguelikeCustomType::CoreChar } },
    };
    auto it = TaskMap.find(task_view);
    if (it == TaskMap.cend()) {
        return false;
    }
    if (msg != it->second.first) {
        return false;
    }
    auto type = it->second.second;
    if (auto custom_it = m_customs.find(type); custom_it == m_customs.cend() || custom_it->second.empty()) {
        return false;
    }

    m_waiting_to_run = type;
    return true;
}

bool asst::RoguelikeCustomStartTaskPlugin::load_params(const json::value& params)
{
    LogTraceFunction;

    m_squad = params.get("squad", "");
    m_collectible_mode_squad = params.get("collectible_mode_squad", "");

    set_custom(RoguelikeCustomType::Squad, m_squad);

    if (params.get("start_with_seed", false)) { // 种子刷钱，强制随心所欲
        set_custom(RoguelikeCustomType::Roles, "随心所欲");
    }
    else {
        set_custom(RoguelikeCustomType::Roles, params.get("roles", "")); // 开局职业组
    }

    set_custom(RoguelikeCustomType::CoreChar, params.get("core_char", ""));                    // 开局干员名
    set_custom(RoguelikeCustomType::UseSupport, params.get("use_support", false) ? "1" : "0"); // 开局干员是否为助战干员
    set_custom(
        RoguelikeCustomType::UseNonfriendSupport,
        params.get("use_nonfriend_support", false) ? "1" : "0"); // 是否可以是非好友助战干员

    return true;
}

void asst::RoguelikeCustomStartTaskPlugin::set_custom(RoguelikeCustomType type, std::string custom)
{
    m_customs.insert_or_assign(type, std::move(custom));
}

bool asst::RoguelikeCustomStartTaskPlugin::_run()
{
    if (m_config->get_run_for_collectible()) {
        set_custom(RoguelikeCustomType::Squad, m_collectible_mode_squad); // 烧水分队
    }
    else {
        set_custom(RoguelikeCustomType::Squad, m_squad); // 开局分队
    }

    const std::unordered_map<RoguelikeCustomType, std::function<bool(void)>> TypeActuator = {
        { RoguelikeCustomType::Squad, std::bind(&RoguelikeCustomStartTaskPlugin::hijack_squad, this) },
        { RoguelikeCustomType::Roles, std::bind(&RoguelikeCustomStartTaskPlugin::hijack_roles, this) },
        { RoguelikeCustomType::CoreChar, std::bind(&RoguelikeCustomStartTaskPlugin::hijack_core_char, this) },
    };

    auto it = TypeActuator.find(m_waiting_to_run);
    if (it == TypeActuator.cend()) {
        return false;
    }

    if (auto custom_it = m_customs.find(it->first); custom_it == m_customs.cend()) {
        return false;
    }
    else if (custom_it->second.empty()) {
        return false;
    }

    return it->second();
}

bool asst::RoguelikeCustomStartTaskPlugin::hijack_squad()
{
    LogTraceFunction;

    constexpr size_t SwipeTimes = 7;
    for (size_t i = 0; i != SwipeTimes; ++i) {
        auto image = ctrler()->get_image();
        OCRer analyzer(image);
        analyzer.set_task_info("RoguelikeCustom-HijackSquad");
        analyzer.set_required({ m_customs[RoguelikeCustomType::Squad] });

        if (!analyzer.analyze()) {
            ProcessTask(*this, { "Roguelike@SquadSlowlySwipeToTheRight" }).run();
            sleep(Task.get("RoguelikeCustom-HijackSquad")->post_delay);
            continue;
        }
        const auto& rect = analyzer.get_result().front().rect;
        ctrler()->click(rect);

        m_config->set_squad(m_customs[RoguelikeCustomType::Squad]);
        return true;
    }
    ProcessTask(*this, { "SwipeToTheLeft" }).run();
    return false;
}

bool asst::RoguelikeCustomStartTaskPlugin::hijack_roles()
{
    LogTraceFunction;

    auto image = ctrler()->get_image();
    OCRer analyzer(image);
    analyzer.set_task_info("RoguelikeCustom-HijackRoles");
    analyzer.set_required({ m_customs[RoguelikeCustomType::Roles] });

    if (!analyzer.analyze()) {
        return false;
    }
    const auto& rect = analyzer.get_result().front().rect;
    ctrler()->click(rect);
    return true;
}

bool asst::RoguelikeCustomStartTaskPlugin::hijack_core_char()
{
    LogTraceFunction;

    if (m_config->get_start_with_seed()) {
        Log.trace("Start with seed");
        auto image = ctrler()->get_image();
        OCRer analyzer(image);
        analyzer.set_task_info("RoguelikeCustom-HijackCoChar");
        analyzer.set_roi({ 186, 500, 913, 200 });
        analyzer.set_required({ "招募" });
        if (!analyzer.analyze()) {
            return false;
        }
        const auto& role_rect = analyzer.get_result().front().rect;
        ctrler()->click(role_rect);

        m_config->set_use_support(m_customs[RoguelikeCustomType::UseSupport] == "1");
        m_config->set_use_nonfriend_support(m_customs[RoguelikeCustomType::UseNonfriendSupport] == "1");
        m_config->set_core_char(m_customs[RoguelikeCustomType::CoreChar]);
        return true;
    }

    const std::string& char_name = m_customs[RoguelikeCustomType::CoreChar];

    static const std::unordered_map<battle::Role, std::string> RoleOcrNameMap = {
        { battle::Role::Caster, "术师" }, { battle::Role::Medic, "医疗" },   { battle::Role::Pioneer, "先锋" },
        { battle::Role::Sniper, "狙击" }, { battle::Role::Special, "特种" }, { battle::Role::Support, "辅助" },
        { battle::Role::Tank, "重装" },   { battle::Role::Warrior, "近卫" }
    };
    const auto& role = BattleData.get_role(char_name);
    auto role_iter = RoleOcrNameMap.find(role);
    if (role_iter == RoleOcrNameMap.cend()) {
        Log.error("Unknown role", char_name, static_cast<int>(role));
        return false;
    }
    // select role
    const std::string& role_ocr_name = role_iter->second;
    Log.info("role", role_ocr_name);
    auto image = ctrler()->get_image();
    OCRer analyzer(image);
    analyzer.set_task_info("RoguelikeCustom-HijackCoChar");
    analyzer.set_required({ role_ocr_name });
    if (!analyzer.analyze()) {
        return false;
    }
    const auto& role_rect = analyzer.get_result().front().rect;
    ctrler()->click(role_rect);

    sleep(Task.get("RoguelikeCustom-HijackCoChar")->pre_delay);

    m_config->set_use_support(m_customs[RoguelikeCustomType::UseSupport] == "1");
    m_config->set_use_nonfriend_support(m_customs[RoguelikeCustomType::UseNonfriendSupport] == "1");
    m_config->set_core_char(char_name);
    return true;
}
