/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "DeterministicStateTraceCli.h"

#include <iostream>

int main(int argc, char** argv)
{
    return RoR::DeterministicStateTrace::RunComparisonCli(
        argc,
        argv,
        std::cout,
        std::cerr);
}
