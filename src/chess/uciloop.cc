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
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "utils/exception.h"
#include "utils/logging.h"
#include "utils/string.h"
#include "version.h"

namespace lczero {
namespace {

// ───────────────────────────────────────── Options
const OptionId kUciChess960{
    "chess960", "UCI_Chess960",
    "Castling moves are encoded as \"king takes rook\"."};
const OptionId kShowWDL{"show-wdl", "UCI_ShowWDL",
                        "Show win, draw and lose probability."};
const OptionId kShowMovesleft{"show-movesleft", "UCI_ShowMovesLeft",
                              "Show estimated moves left."};

// ─────────────────────────────── Known commands / keywords
const std::unordered_map<std::string, std::unordered_set<std::string>>
    kKnownCommands = {
        {{"uci"}, {}},
        {{"isready"}, {}},
        {{"setoption"}, {"name", "value", "context"}},
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

// ─────────────────────────────── setoption helper
constexpr std::string_view kNameTok    = "name ";
constexpr std::string_view kValueTok   = " value ";
constexpr std::string_view kContextTok = " context ";

std::unordered_map<std::string, std::string>
ParseSetOption(std::string_view rest) {
  rest = utils::string::Trim(rest);

  if (!rest.starts_with(kNameTok))
    throw Exception("Malformed setoption (expected \"name\")");

  const size_t v_pos = rest.find(kValueTok);
  if (v_pos == std::string_view::npos)
    throw Exception("Malformed setoption (missing \"value\")");

  const size_t c_pos = rest.rfind(kContextTok);
  const bool   has_ctx = c_pos != std::string_view::npos && c_pos > v_pos;

  auto trim = [](std::string_view sv) { return utils::string::Trim(sv); };

  std::unordered_map<std::string, std::string> p;

  // -------- name -----------------------------------------------------------
  const size_t name_start = kNameTok.size();
  p["name"] = std::string(trim(rest.substr(name_start,
                                           v_pos - name_start)));
  if (p["name"].empty())
    throw Exception("Empty option name");

  // -------- value ----------------------------------------------------------
  const size_t val_start = v_pos + kValueTok.size();
  p["value"] = std::string(trim(
      rest.substr(val_start,
                  has_ctx ? c_pos - val_start : std::string_view::npos)));

  if (p["value"].empty())
    throw Exception("Empty value for option \"" + p["name"] + "\"");

  // -------- context (optional) --------------------------------------------
  if (has_ctx) {
    const size_t ctx_start = c_pos + kContextTok.size();
    p["context"] = std::string(trim(rest.substr(ctx_start)));
    if (p["context"].empty())
      throw Exception("Empty context for \"" + p["name"] + "\"");
  }
  return p;
}

// ─────────────────────────────── Generic command parser
std::pair<std::string, std::unordered_map<std::string, std::string>>
ParseCommand(const std::string& line) {
  std::istringstream iss(line);
  std::string cmd;
  iss >> cmd >> std::ws;

  if (cmd.empty()) return {};

  if (cmd == "setoption") {
    std::string rest;
    std::getline(iss, rest);
    return {cmd, ParseSetOption(rest)};
  }

  const auto known = kKnownCommands.find(cmd);
  if (known == kKnownCommands.end())
    throw Exception("Unknown command: " + cmd);

  std::unordered_map<std::string, std::string> params;
  std::string* value = nullptr;
  std::string tok;
  std::string ws;

  while (iss >> tok) {
    if (known->second.count(tok)) {
      value = &params[tok];
      iss >> std::ws;
      ws.clear();
    } else {
      if (!value) throw Exception("Unexpected token: " + tok);
      *value += ws + tok;
      ws = " ";
    }
  }
  return {known->first, params};
}

// ─────────────────────────────── Small helpers
inline std::string GetOrEmpty(const std::unordered_map<std::string, std::string>& m,
                              const std::string& k) {
  auto it = m.find(k);
  return it == m.end() ? std::string() : it->second;
}

int GetNumeric(const std::unordered_map<std::string, std::string>& m,
               const std::string& key) {
  auto it = m.find(key);
  if (it == m.end()) throw Exception("Unexpected error");
  try {
    if (it->second.empty()) throw Exception("expected value after " + key);
    return std::stoi(it->second);
  } catch (const std::invalid_argument&) {
    throw Exception("invalid value " + it->second);
  } catch (const std::out_of_range&) {
    throw Exception("out of range value " + it->second);
  }
}

bool ContainsKey(const std::unordered_map<std::string, std::string>& m,
                 const std::string& key) {
  return m.find(key) != m.end();
}

// ────────────────────────────────────────────────────────────────────────────
}  // namespace

// ─────────────────────────────── UciLoop implementation
UciLoop::UciLoop(StringUciResponder* uci_responder,
                 OptionsParser*       options,
                 EngineControllerBase* engine)
    : uci_responder_(uci_responder),
      options_(options),
      engine_(engine) {
  engine_->RegisterUciResponder(uci_responder_);
}

UciLoop::~UciLoop() {
  engine_->UnregisterUciResponder(uci_responder_);
}

bool UciLoop::DispatchCommand(
    const std::string&                                   command,
    const std::unordered_map<std::string, std::string>&  params) {
  if (command == "uci") {
    uci_responder_->SendId();
    for (const auto& option : options_->ListOptionsUci())
      uci_responder_->SendRawResponse(option);
    uci_responder_->SendRawResponse("uciok");

  } else if (command == "isready") {
    engine_->EnsureReady();
    uci_responder_->SendRawResponse("readyok");

  } else if (command == "setoption") {
    options_->SetUciOption(GetOrEmpty(params, "name"),
                           GetOrEmpty(params, "value"),
                           GetOrEmpty(params, "context"));

  } else if (command == "ucinewgame") {
    engine_->NewGame();

  } else if (command == "position") {
    if (ContainsKey(params, "fen") == ContainsKey(params, "startpos"))
      throw Exception("Position requires either fen or startpos");

    const std::vector<std::string> moves =
        StrSplitAtWhitespace(GetOrEmpty(params, "moves"));
    const std::string fen = GetOrEmpty(params, "fen");
    engine_->SetPosition(fen.empty() ? ChessBoard::kStartposFen : fen, moves);

  } else if (command == "go") {
    GoParams go_params;
    if (ContainsKey(params, "infinite")) {
      if (!GetOrEmpty(params, "infinite").empty())
        throw Exception("Unexpected token " + GetOrEmpty(params, "infinite"));
      go_params.infinite = true;
    }
    if (ContainsKey(params, "searchmoves"))
      go_params.searchmoves =
          StrSplitAtWhitespace(GetOrEmpty(params, "searchmoves"));
    if (ContainsKey(params, "ponder")) {
      if (!GetOrEmpty(params, "ponder").empty())
        throw Exception("Unexpected token " + GetOrEmpty(params, "ponder"));
      go_params.ponder = true;
    }
#define UCIGOOPTION(x)                    \
  if (ContainsKey(params, #x)) {          \
    go_params.x = GetNumeric(params, #x); \
  }
    UCIGOOPTION(wtime);  UCIGOOPTION(btime);    UCIGOOPTION(winc);
    UCIGOOPTION(binc);   UCIGOOPTION(movestogo);UCIGOOPTION(depth);
    UCIGOOPTION(mate);   UCIGOOPTION(nodes);    UCIGOOPTION(movetime);
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
  auto cmd = ParseCommand(line);
  if (cmd.first.empty()) return true;  // ignore blank line
  return DispatchCommand(cmd.first, cmd.second);
}

// ─────────────────────────────── Responder helpers
void StringUciResponder::PopulateParams(OptionsParser* options) {
  options->Add<BoolOption>(kUciChess960)   = false;
  options->Add<BoolOption>(kShowWDL)       = true;
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
  if (info->player != -1)  res += " player " + std::to_string(info->player);
  if (info->game_id != -1) res += " gameid " + std::to_string(info->game_id);
  if (info->is_black)
    res += " side " + std::string(*info->is_black ? "black" : "white");
  SendRawResponse(res);
}

void StringUciResponder::OutputThinkingInfo(std::vector<ThinkingInfo>* infos) {
  std::vector<std::string> out;
  const bool c960 = IsChess960();
  for (const auto& info : *infos) {
    std::string res = "info";
    if (info.player != -1)  res += " player " + std::to_string(info.player);
    if (info.game_id != -1) res += " gameid " + std::to_string(info.game_id);
    if (info.is_black)
      res += " side " + std::string(*info.is_black ? "black" : "white");
    if (info.depth >= 0)
      res += " depth " + std::to_string(std::max(info.depth, 1));
    if (info.seldepth >= 0) res += " seldepth " + std::to_string(info.seldepth);
    if (info.time >= 0)     res += " time "   + std::to_string(info.time);
    if (info.nodes >= 0)    res += " nodes "  + std::to_string(info.nodes);
    if (info.mate)          res += " score mate " + std::to_string(*info.mate);
    if (info.score)         res += " score cp "   + std::to_string(*info.score);
    if (info.wdl && options_ && options_->Get<bool>(kShowWDL))
      res += " wdl " + std::to_string(info.wdl->w) + ' ' +
             std::to_string(info.wdl->d) + ' ' +
             std::to_string(info.wdl->l);
    if (info.moves_left && options_ && options_->Get<bool>(kShowMovesleft))
      res += " movesleft " + std::to_string(*info.moves_left);
    if (info.hashfull >= 0) res += " hashfull " + std::to_string(info.hashfull);
    if (info.nps >= 0)      res += " nps "      + std::to_string(info.nps);
    if (info.tb_hits >= 0)  res += " tbhits "   + std::to_string(info.tb_hits);
    if (info.multipv >= 0)  res += " multipv "  + std::to_string(info.multipv);

    if (!info.pv.empty()) {
      res += " pv";
      for (const auto& m : info.pv) res += ' ' + m.ToString(c960);
    }
    if (!info.comment.empty()) res += " string " + info.comment;
    out.push_back(std::move(res));
  }
  SendRawResponses(out);
}

// ─────────────────────────────── Stdout responder
void StdoutUciResponder::SendRawResponses(
    const std::vector<std::string>& lines) {
  static std::mutex output_mutex;
  std::lock_guard<std::mutex> lock(output_mutex);
  for (const auto& line : lines) {
    LOGFILE << "<< " << line;
    std::cout << line << std::endl;
  }
}

}  // namespace lczero

