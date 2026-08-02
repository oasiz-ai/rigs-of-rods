/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PbrReference.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "strict renderer FP environment test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

float RuntimeMinimumNormal() noexcept {
  volatile float value = (std::numeric_limits<float>::min)();
  return value;
}

float RuntimeSubnormal() noexcept {
  volatile float minimum_normal = RuntimeMinimumNormal();
  volatile float half = 0.5F;
  volatile float subnormal = minimum_normal * half;
  return subnormal;
}

void TestExecutablePreservesStrictSubnormalArithmetic() {
  const float minimum_normal = RuntimeMinimumNormal();
  const float subnormal = RuntimeSubnormal();
  Require(subnormal > 0.0F && subnormal < minimum_normal,
          "the process flushed a finite subnormal result to zero");
}

void TestPbrReferenceReceivesSubnormalDirections() {
  using namespace RoR::Render;

  PbrDirectReferenceInput input;
  input.shading_normal = {RuntimeSubnormal(), 0.0F, 0.0F};
  PbrDirectReferenceResult output;
  const ValidationResult validation =
      EvaluatePbrDirectReference(input, output);
  Require(validation.ok(),
          "the strict PBR oracle rejected a nonzero subnormal direction");
  Require(IsFinite(output.direct_response),
          "the strict PBR oracle produced a non-finite response");
}

} // namespace

int main() {
  TestExecutablePreservesStrictSubnormalArithmetic();
  TestPbrReferenceReceivesSubnormalDirections();
  std::cout << "strict renderer floating-point environment tests passed\n";
  return EXIT_SUCCESS;
}
