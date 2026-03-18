////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2006 - 2021, Tomas Babej, Paul Beckingham, Federico Hernandez.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// https://www.opensource.org/licenses/mit-license.php
//
////////////////////////////////////////////////////////////////////////////////

#include <cmake.h>
// cmake.h include header must come first

#include <CmdConfig.h>
#include <Context.h>

#include <algorithm>

////////////////////////////////////////////////////////////////////////////////
bool CmdConfig::setConfigVariable(const std::string& name, const std::string& value) {
  Context::getContext().config.set(name, value);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
int CmdConfig::unsetConfigVariable(const std::string& name) {
  auto& config = Context::getContext().config;
  if (config.has(name)) {
    config.erase(name);
    return 0;
  }
  return 2;
}

////////////////////////////////////////////////////////////////////////////////
CmdLegacyConfig::CmdLegacyConfig() {
  _keyword = "config";
  _usage = "task          config ...";
  _description = "DEPRECATED: Use rc.<key>:<value> overrides instead";
  _read_only = true;
  _displays_id = false;
  _needs_gc = false;
  _needs_recur_update = false;
  _uses_context = false;
  _accepts_filter = false;
  _accepts_modifications = false;
  _accepts_miscellaneous = true;
  _category = Command::Category::misc;
}

////////////////////////////////////////////////////////////////////////////////
int CmdLegacyConfig::execute(std::string&) {
  throw std::string(
      "'task config' is not supported. Use rc.<key>:<value> overrides on the command line instead.");
}

////////////////////////////////////////////////////////////////////////////////
CmdCompletionConfig::CmdCompletionConfig() {
  _keyword = "_config";
  _usage = "task          _config";
  _description = "Lists all supported configuration variables, for completion purposes";
  _read_only = true;
  _displays_id = false;
  _needs_gc = false;
  _needs_recur_update = false;
  _uses_context = false;
  _accepts_filter = false;
  _accepts_modifications = false;
  _accepts_miscellaneous = false;
  _category = Command::Category::internal;
}

////////////////////////////////////////////////////////////////////////////////
int CmdCompletionConfig::execute(std::string& output) {
  auto configs = Context::getContext().config.all();
  std::sort(configs.begin(), configs.end());

  for (const auto& config : configs) output += config + '\n';

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
