/*
  This file is part of Leela Chess Zero.
  Copyright (C) 2018 The LCZero Authors

  Leela Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Leela Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Leela Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7

  If you modify this Program, or any covered work, by linking or
  combining it with NVIDIA Corporation's libraries from the NVIDIA CUDA
  Toolkit and the NVIDIA CUDA Deep Neural Network library (or a
  modified version of those libraries), containing parts covered by the
  terms of the respective license agreement, the licensors of this
  Program grant you additional permission to convey the resulting work.
*/

#include "uciloop.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <cctype>

#include "utils/exception.h"
#include "utils/logging.h"
#include "utils/string.h"
#include "version.h"

namespace lczero {
namespace {

const OptionId kUciChess960{
    "chess960", "UCI_Chess960",
    "Castling moves are encoded as \"king takes rook\"."};
const OptionId kShowWDL{"show-wdl", "UCI_ShowWDL",
                        "Show win, draw and lose probability."};
const OptionId kShowMovesleft{"show-movesleft", "UCI_ShowMovesLeft",
                              "Show estimated moves left."};

const std::unordered_map<std::string, std::unordered_set<std::string>>
    kKnownCommands = {
        {{"uci"}, {}},
        {{"isready"}, {}},
        {{"setoption"}, {"context", "name", "value"}},
        {{"ucinewgame"}, {}},
        {{"position"}, {"fen", "startpos", "moves"}},
        {{"go"},
         {"infinite", "wtime", "btime", "winc", "binc", "movestogo", "depth",
          "mate", "nodes", "movetime", "searchmoves", "ponder"}},
        {{"stop"}, {}},
        {{"ponderhit"}, {}},
        {{"quit"}, {}},
        {{"xyzzy"}, {}},
        {{"fen"}, {}},
};


size_t FindBoundedKeywordOccurrence(std::string_view text, std::string_view keyword,
                                   size_t search_start_pos = 0, bool find_last = false) {
    size_t best_pos = std::string_view::npos;

    if (find_last) {
        for (size_t i = text.length(); i > search_start_pos; --i) {
            size_t current_scan_pos = i - 1;
            if (current_scan_pos < keyword.length() -1 ) break;
            if (text.length() - current_scan_pos < keyword.length()) {
                 if (keyword.length() > text.length()) continue;
                 current_scan_pos = text.length() - keyword.length();
            }


            size_t pos = text.rfind(keyword, current_scan_pos);
            if (pos == std::string_view::npos || pos < search_start_pos) { 
            }
             if (pos == std::string_view::npos) continue;


            bool space_before = (pos == 0 || std::isspace(static_cast<unsigned char>(text[pos - 1])));
            bool space_after = ((pos + keyword.length()) == text.length() || std::isspace(static_cast<unsigned char>(text[pos + keyword.length()])));

            if (space_before && space_after) {
                 return pos;
            }
            if (pos == 0) break;
            i = pos;
        }
        return std::string_view::npos;
    } else {
        size_t current_search_offset = search_start_pos;
        while (current_search_offset < text.length()) {
            size_t pos = text.find(keyword, current_search_offset);
            if (pos == std::string_view::npos) return std::string_view::npos;

            bool space_before = (pos == 0 || std::isspace(static_cast<unsigned char>(text[pos - 1])));
            bool space_after = ((pos + keyword.length()) == text.length() || std::isspace(static_cast<unsigned char>(text[pos + keyword.length()])));
            
            if (space_before && space_after) return pos;
            current_search_offset = pos + 1;
        }
    }
    return std::string_view::npos;
}


std::unordered_map<std::string, std::string> ParseSetOption(
    std::string_view command_args) {
  std::unordered_map<std::string, std::string> params;
  command_args = utils::string::Trim(command_args);

  constexpr std::string_view kNameTok = "name";
  constexpr std::string_view kValueTok = "value";
  constexpr std::string_view kContextTok = "context";

  // 1. Find "name" keyword
  if (!(command_args.rfind(kNameTok, 0) == 0 && command_args.length() > kNameTok.length() && std::isspace(static_cast<unsigned char>(command_args[kNameTok.length()])))) {
    throw Exception("Malformed setoption (expected 'name')");
  }
  
  command_args.remove_prefix(kNameTok.length());
  command_args = utils::string::Trim(command_args);

  // 2. Find "value" keyword (" value ")
  size_t value_keyword_pos = FindBoundedKeywordOccurrence(command_args, kValueTok, 0, false);
  if (value_keyword_pos == std::string_view::npos) {
    throw Exception("Malformed setoption (missing 'value')");
  }
  
  std::string_view option_name_sv = command_args.substr(0, value_keyword_pos);
  option_name_sv = utils::string::Trim(option_name_sv);

  if (option_name_sv.empty()) {
    throw Exception("Empty option name");
  }
  params["name"] = std::string(option_name_sv);

  command_args.remove_prefix(value_keyword_pos + kValueTok.length());
  command_args = utils::string::Trim(command_args);

  // 3. Find "context" keyword (optional, last occurrence, " context ")
  size_t context_keyword_pos = FindBoundedKeywordOccurrence(command_args, kContextTok, 0, true);

  if (context_keyword_pos != std::string_view::npos) {
    std::string_view option_value_sv = command_args.substr(0, context_keyword_pos);
    option_value_sv = utils::string::Trim(option_value_sv);

    command_args.remove_prefix(context_keyword_pos + kContextTok.length());
    command_args = utils::string::Trim(command_args);
    std::string_view option_context_sv = command_args;
    
    if (option_context_sv.empty()) {
      throw Exception("Empty context for '" + params["name"] + "'");
    }
    params["context"] = std::string(option_context_sv);
    params["value"] = std::string(option_value_sv); 
  } else {
    std::string_view option_value_sv = command_args;
    option_value_sv = utils::string::Trim(option_value_sv);
    params["value"] = std::string(option_value_sv);
  }

  // 4. Final check for empty value
  auto it_val = params.find("value");
  if (it_val == params.end() || it_val->second.empty()) {
      throw Exception("Empty option value");
  }

  return params;
}

std::pair<std::string, std::unordered_map<std::string, std::string>>
ParseCommand(const std::string& line) {
  std::unordered_map<std::string, std::string> params;
  std::string cmd_name;
  
  std::string_view line_sv = line;
  line_sv = utils::string::Trim(line_sv);

  if (line_sv.empty()) return {};

  size_t space_pos = line_sv.find_first_of(" \t\n\r\f\v");
  if (space_pos == std::string_view::npos) {
    cmd_name = std::string(line_sv);
    line_sv = ""; 
  } else {
    cmd_name = std::string(line_sv.substr(0, space_pos));
    line_sv.remove_prefix(space_pos);
    line_sv = utils::string::Trim(line_sv); 
  }
  
  const auto command_iter = kKnownCommands.find(cmd_name);
  if (command_iter == kKnownCommands.end()) {
    if (cmd_name.empty() && line_sv.empty()) return {};
    throw Exception("Unknown command: '" + cmd_name + "' from line: '" + line + "'");
  }

  if (cmd_name == "setoption") {
    params["args_for_setoption"] = std::string(line_sv);
  } else {
    std::string* value_ptr = nullptr;
    std::istringstream iss(std::string(line_sv));
    std::string token;
    std::string accumulated_whitespace; 

    while (iss >> token) { // Tokenizes by whitespace
        auto param_keyword_iter = command_iter->second.find(token);
        if (param_keyword_iter == command_iter->second.end()) {
            if (!value_ptr) {
                 throw Exception("Unexpected token: " + token + " in command " + cmd_name);
            }
            *value_ptr += accumulated_whitespace + token;
            accumulated_whitespace = " "; 
        } else {
            value_ptr = &params[token]; 
            *value_ptr = "";
            accumulated_whitespace = ""; 
        }
    }
  }
  return {command_iter->first, params}; 
}

std::string GetOrEmpty(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& key) {
  const auto iter = params.find(key);
  if (iter == params.end()) return {};
  return iter->second;
}

int GetNumeric(const std::unordered_map<std::string, std::string>& params,
               const std::string& key) {
  const auto iter = params.find(key);
  if (iter == params.end()) {
    throw Exception("Unexpected error");
  }
  const std::string& str = iter->second;
  try {
    if (str.empty()) {
      throw Exception("expected value after " + key);
    }
    return std::stoi(str);
  } catch (std::invalid_argument&) {
    throw Exception("invalid value " + str);
  } catch (const std::out_of_range&) {
    throw Exception("out of range value " + str);
  }
}

bool ContainsKey(const std::unordered_map<std::string, std::string>& params,
                 const std::string& key) {
  return params.find(key) != params.end();
}
}  // namespace

UciLoop::UciLoop(StringUciResponder* uci_responder, OptionsParser* options,
                 EngineControllerBase* engine)
    : uci_responder_(uci_responder), options_(options), engine_(engine) {
  engine_->RegisterUciResponder(uci_responder_);
}

UciLoop::~UciLoop() { engine_->UnregisterUciResponder(uci_responder_); }

bool UciLoop::DispatchCommand(
    const std::string& command,
    const std::unordered_map<std::string, std::string>& params) {
  if (command == "uci") {
    uci_responder_->SendId();
    for (const auto& option : options_->ListOptionsUci()) {
      uci_responder_->SendRawResponse(option);
    }
    uci_responder_->SendRawResponse("uciok");
  } else if (command == "isready") {
    engine_->EnsureReady();
    uci_responder_->SendRawResponse("readyok");
  } else if (command == "setoption") {
    std::string setoption_args_str = GetOrEmpty(params, "args_for_setoption");
    
    auto parsed_setoption_params = ParseSetOption(setoption_args_str);
    
    options_->SetUciOption(
        parsed_setoption_params.at("name"),
        parsed_setoption_params.at("value"),
        parsed_setoption_params.count("context") ? parsed_setoption_params.at("context") : "");
  } else if (command == "ucinewgame") {
    engine_->NewGame();
  } else if (command == "position") {
    if (ContainsKey(params, "fen") == ContainsKey(params, "startpos")) {
      throw Exception("Position requires either fen or startpos");
    }
    const std::vector<std::string> moves =
        StrSplitAtWhitespace(GetOrEmpty(params, "moves"));
    const std::string fen = GetOrEmpty(params, "fen");
    engine_->SetPosition(fen.empty() ? ChessBoard::kStartposFen : fen, moves);
  } else if (command == "go") {
    GoParams go_params;
    if (ContainsKey(params, "infinite")) {
      if (!GetOrEmpty(params, "infinite").empty()) {
        throw Exception("Unexpected token " + GetOrEmpty(params, "infinite"));
      }
      go_params.infinite = true;
    }
    if (ContainsKey(params, "searchmoves")) {
      go_params.searchmoves =
          StrSplitAtWhitespace(GetOrEmpty(params, "searchmoves"));
    }
    if (ContainsKey(params, "ponder")) {
      if (!GetOrEmpty(params, "ponder").empty()) {
        throw Exception("Unexpected token " + GetOrEmpty(params, "ponder"));
      }
      go_params.ponder = true;
    }
#define UCIGOOPTION(x)                    \
  if (ContainsKey(params, #x)) {          \
    go_params.x = GetNumeric(params, #x); \
  }
    UCIGOOPTION(wtime);
    UCIGOOPTION(btime);
    UCIGOOPTION(winc);
    UCIGOOPTION(binc);
    UCIGOOPTION(movestogo);
    UCIGOOPTION(depth);
    UCIGOOPTION(mate);
    UCIGOOPTION(nodes);
    UCIGOOPTION(movetime);
#undef UCIGOOPTION
    engine_->Go(go_params);
  } else if (command == "stop") {
    engine_->Stop();
  } else if (command == "ponderhit") {
    engine_->PonderHit();
  } else if (command == "xyzzy") {
    uci_responder_->SendRawResponse("Nothing happens.");
  } else if (command == "quit") {
    return false;
  } else {
    throw Exception("Unknown command: " + command);
  }
  return true;
}

bool UciLoop::ProcessLine(const std::string& line) {
  auto command = ParseCommand(line);
  if (command.first.empty()) return true;
  return DispatchCommand(command.first, command.second);
}

void StringUciResponder::PopulateParams(OptionsParser* options) {
  options->Add<BoolOption>(kUciChess960) = false;
  options->Add<BoolOption>(kShowWDL) = true;
  options->Add<BoolOption>(kShowMovesleft) = false;
  options_ = &options->GetOptionsDict();
}

bool StringUciResponder::IsChess960() const {
  return options_ ? options_->Get<bool>(kUciChess960) : false;
}

void StringUciResponder::SendRawResponse(const std::string& response) {
  SendRawResponses({response});
}

void StringUciResponder::SendId() {
  SendRawResponse("id name Lc0 v" + GetVersionStr());
  SendRawResponse("id author The LCZero Authors.");
}

void StringUciResponder::OutputBestMove(BestMoveInfo* info) {
  const bool c960 = IsChess960();
  std::string res = "bestmove " + info->bestmove.ToString(c960);
  if (!info->ponder.is_null()) res += " ponder " + info->ponder.ToString(c960);
  if (info->player != -1) res += " player " + std::to_string(info->player);
  if (info->game_id != -1) res += " gameid " + std::to_string(info->game_id);
  if (info->is_black)
    res += " side " + std::string(*info->is_black ? "black" : "white");
  SendRawResponse(res);
}

void StringUciResponder::OutputThinkingInfo(std::vector<ThinkingInfo>* infos) {
  std::vector<std::string> reses;
  const bool c960 = IsChess960();
  for (const auto& info : *infos) {
    std::string res = "info";
    if (info.player != -1) res += " player " + std::to_string(info.player);
    if (info.game_id != -1) res += " gameid " + std::to_string(info.game_id);
    if (info.is_black)
      res += " side " + std::string(*info.is_black ? "black" : "white");
    if (info.depth >= 0)
      res += " depth " + std::to_string(std::max(info.depth, 1));
    if (info.seldepth >= 0) res += " seldepth " + std::to_string(info.seldepth);
    if (info.time >= 0) res += " time " + std::to_string(info.time);
    if (info.nodes >= 0) res += " nodes " + std::to_string(info.nodes);
    if (info.mate) res += " score mate " + std::to_string(*info.mate);
    if (info.score) res += " score cp " + std::to_string(*info.score);
    if (info.wdl && options_ && options_->Get<bool>(kShowWDL)) {
      res += " wdl " + std::to_string(info.wdl->w) + " " +
             std::to_string(info.wdl->d) + " " + std::to_string(info.wdl->l);
    }
    if (info.moves_left && options_ && options_->Get<bool>(kShowMovesleft)) {
      res += " movesleft " + std::to_string(*info.moves_left);
    }
    if (info.hashfull >= 0) res += " hashfull " + std::to_string(info.hashfull);
    if (info.nps >= 0) res += " nps " + std::to_string(info.nps);
    if (info.tb_hits >= 0) res += " tbhits " + std::to_string(info.tb_hits);
    if (info.multipv >= 0) res += " multipv " + std::to_string(info.multipv);

    if (!info.pv.empty()) {
      res += " pv";
      for (const auto& move : info.pv) res += " " + move.ToString(c960);
    }
    if (!info.comment.empty()) res += " string " + info.comment;
    reses.push_back(std::move(res));
  }
  SendRawResponses(reses);
}

void StdoutUciResponder::SendRawResponses(
    const std::vector<std::string>& responses) {
  static std::mutex output_mutex;
  std::lock_guard<std::mutex> lock(output_mutex);
  for (auto& response : responses) {
    LOGFILE << "<< " << response;
    std::cout << response << std::endl;
  }
}

}  // namespace lczero
