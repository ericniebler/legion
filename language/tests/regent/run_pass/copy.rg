-- Copyright 2023 Stanford University
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

import "regent"

task k() : int
  var r = region(ispace(ptr, 1), int)
  var x = dynamic_cast(ptr(int, r), 0)
  var s = region(ispace(ptr, 1), int)
  var y = dynamic_cast(ptr(int, s), 0)

  @x = 123
  @y = 456

  copy(r, s)
  @y *= 2
  copy(s, r, +)

  return @x
end

task main()
  regentlib.assert(k() == 369, "test failed")
end
regentlib.start(main)
