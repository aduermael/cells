-- Double every value in column B for rows 2–100 if numeric.
for row = 2, 100 do
  local ref = "B" .. row
  local c = getCell(ref)
  if c == nil then
    break
  end
  if type(c.value) == "number" then
    setCell(ref, c.value * 2)
  end
end
