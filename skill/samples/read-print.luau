-- Read A1 and print its value (script-only friendly).
local c = getCell("A1")
if c == nil then
  print("A1 is empty")
else
  print("A1 =", c.value)
end
