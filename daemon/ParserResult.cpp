/* Copyright (C) 2014-2023 by Arm Limited. All rights reserved. */

#include "GatorCLIParser.h"

#include <algorithm>
#include <vector>

#include <ParserResult.h>

const std::vector<std::pair<std::string, std::optional<std::string>>> & ParserResult::getArgValuePairs() const
{
    return argValuePairs;
}

void ParserResult::addArgValuePair(std::pair<std::string, std::optional<std::string>> argValuePair)
{
    argValuePairs.emplace_back(std::move(argValuePair));
}

void ParserResult::moveAppArgToEndOfVector()
{
    sort(argValuePairs.begin(),
         argValuePairs.end(),
         [](const std::pair<std::string, std::optional<std::string>> & arg0, //
            const std::pair<std::string, std::optional<std::string>> & arg1) -> bool {
             auto isFirstNotEqualToApp =
                 ((arg0.first != APP.name) || (arg0.first.length() == 1 && arg0.first[0] != (char) APP.val));
             auto isSecondEqualToApp =
                 ((arg1.first == APP.name) || (arg1.first.length() == 1 && arg1.first[0] == (char) APP.val));
             return isFirstNotEqualToApp && isSecondEqualToApp;
         });
}

void ParserResult::parsingFailed()
{
    mode = ExecutionMode::EXIT;
    argValuePairs.clear();
}

bool ParserResult::ok() const
{
    return mode != ExecutionMode::EXIT;
}
