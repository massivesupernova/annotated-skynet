local args = {} 
-- 	each time, `word` get a non-space character string
for word in string.gmatch(..., "%S+") do
	-- append the element `word` to the tail
	table.insert(args, word)
end

SERVICE_NAME = args[1]

local main, pattern

local err = {}
-- default LUA_SERVICE = "./service/?.lua"
for pat in string.gmatch(LUA_SERVICE, "([^;]+);*") do
	-- find '?' and replace it by SERVICE_NAME
	-- so get "./service/bootstrap.lua" for example
	local filename = string.gsub(pat, "?", SERVICE_NAME)
	-- return compiled function or nil and error message
	local f, msg = loadfile(filename)
	if not f then
		-- if fail then continue try next search path
		table.insert(err, msg)
	else
		-- otherwise record the path and the compiled function and break the loop
		pattern = pat
		main = f
		break
	end
end

-- if related the SERVICE_NAME not found, throw the error
if not main then
	error(table.concat(err, "\n"))
end

-- backup the LUA_PATH and LUA_CPATH
-- and clear the paths of LUA_SERVICE LUA_PATH LUA_CPATH to nil
LUA_SERVICE = nil
package.path , LUA_PATH = LUA_PATH
package.cpath , LUA_CPATH = LUA_CPATH

local service_path = string.match(pattern, "(.*/)[^/?]+$")

if service_path then
	service_path = string.gsub(service_path, "?", args[1])
	package.path = service_path .. "?.lua;" .. package.path
	SERVICE_PATH = service_path
else
	local p = string.match(pattern, "(.*/).+$")
	SERVICE_PATH = p
end

-- default LUA_PRELOAD = "./examples/preload.lua"
if LUA_PRELOAD then
	local f = assert(loadfile(LUA_PRELOAD))
	-- if has preload.lua, then call the function with all arguments
	f(table.unpack(args))
	LUA_PRELOAD = nil
end

-- call lua service function will all arguments except the first one (it is the service name)
main(select(2, table.unpack(args)))
