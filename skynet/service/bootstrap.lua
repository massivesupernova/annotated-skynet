-- load the module "./lualib/skynet.lua"
local skynet = require "skynet"
-- load the module "./lualib/skynet/harbor.lua"
local harbor = require "skynet.harbor"
-- load the module "./lualib/skynet/manager.lua"
require "skynet.manager"	-- import skynet.launch, ...
-- load the module "./luaclib/memory.so" <= "./lualib-src/lua-memory.c"
local memory = require "memory"

skynet.start(function()
	local sharestring = tonumber(skynet.getenv "sharestring")
  -- call to lexpandshrtbl(L) in lua-memory.c
	memory.ssexpand(sharestring or 4096)

	local standalone = skynet.getenv "standalone"
  -- launch skynet_snlua.c service to load launcher.lua
	local launcher = assert(skynet.launch("snlua","launcher"))
  -- call to cmd_name and skynet_handle_namehandle(handle, "launcher")
  -- insert the name into global handle_storage H->name[i]
	skynet.name(".launcher", launcher)

	local harbor_id = tonumber(skynet.getenv "harbor")
	if harbor_id == 0 then
		assert(standalone ==  nil)
		standalone = true
		skynet.setenv("standalone", "true")

		local ok, slave = pcall(skynet.newservice, "cdummy")
		if not ok then
			skynet.abort()
		end
		skynet.name(".cslave", slave)

	else
		if standalone then
			if not pcall(skynet.newservice,"cmaster") then
				skynet.abort()
			end
		end

		local ok, slave = pcall(skynet.newservice, "cslave")
		if not ok then
			skynet.abort()
		end
		skynet.name(".cslave", slave)
	end

  -- 1. single node network (harbor is 0) has a master node, 
  --    will start cdummy and datacenterd services
  -- 2. in multiple nodes network, master node is responsible for scheduling, 
  --    will start cmaster and datacenterd services
  -- 3. in multiple nodes netwrok, both master and slave node will start cslave service
  -- 4. finally, start service_mgr service and user start (or main if not given) service
  
	if standalone then
		local datacenter = skynet.newservice "datacenterd"
		skynet.name("DATACENTER", datacenter)
	end
	skynet.newservice "service_mgr"
	pcall(skynet.newservice,skynet.getenv "start" or "main")
	skynet.exit()
end)
