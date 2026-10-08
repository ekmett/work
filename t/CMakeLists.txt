# SPDX-FileCopyrightText: 2026 Edward Kmett <ekmett@gmail.com>
# SPDX-License-Identifier: BSD-2-Clause OR Apache-2.0
add_executable(work-test work-test.cc)
target_link_libraries(work-test PRIVATE work::work)
add_test(NAME work-test COMMAND work-test)
set_tests_properties(work-test PROPERTIES TIMEOUT 60)
