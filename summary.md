
### Message Sources
- skynet_error(ctx, msg, ...)
- skynet_send(ctx, source, dest, type, session, data, sz)
- skynet_sendname(ctx, source, addr, type, session, data, sz)
- thread_socket(para) -> skynet_socket_poll()
- skynet.timeout(time, func)
- skynet.sleep(time)

```lua
skynet_mq_push(queue, message)  
 <- skynet_context_push(handle, message)  
     <- skynet_error(ctx, msg, ...)  
     <- skynet_send(ctx, source, dest, type, session, data, sz)  
     <- forward_message(type, padding, socket_message) <- skynet_socket_poll() <- thread_socket(para)  
     <- dispatch_list(timer_node) <- skynet_updatetime() <- thread_timer(para)  
     <- skynet_timeout(handle, time, session) <- skynet.timeout(time, func) skynet.sleep(time)  
 <- skynet_context_send(ctx, msg, sz, source, type, session)  
     <- skynet_harbor_send(remote_message, source, session)  
         <- skynet_send(ctx, source, dest, type, session, data, sz)  
         <- skynet_sendname(ctx, source, addr, type, session, data, sz)    
```

### Message Handling

```lua
bootstrap() -> skynet_context_dispatchall(ctx)
thread_worker(para) -> skynet_context_message_dispatch(monitor, queue, weight)    
 -> ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz)
``` 

### Timer Message



```lua
function skynet.timeout(ti, func)
  local session = c.intcommand("TIMEOUT", ti)
  session_id_coroutine[session] = co_create(func)
end
function skynet.sleep(ti)
  local session = c.intcommand("TIMEOUT", ti)
  coroutine_yield("SLEEP", session)
  sleep_session[coroutine.running()] = nil
end
```
```c
// @param: integer string from `ti` above
const char* skynet_command(struct skynet_context* ctx, const char* cmd, const char* param) {
  // find cmd handle function according to `cmd` string
  // return func(ctx, param) => 
  return cmd_timeout(ctx, param);
}
const char* cmd_timeout(struct skynet_context* ctx, const char* param) {
  int ti = get integer from param;
  int session = skynet_context_newsession(ctx);
  skynet_timeout(ctx->handle, ti, session);
  return string representation of session;
}
int skynet_timeout(uint32_t handle, int time, int session) {
  if (time <= 0) {
    struct skynet_message msg = {.source = 0, .session = session, 
      .data = NULL, .sz = PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT };
    skynet_context_push(handle, &msg);
  } else {
    struct timer_event event = {.handle = handle, .session = session};
    timer_add(TI, &event, sizeof(event), time);
  }
  return session;
}
```

### Create Service

```lua
-- start a service and return the service's handle
function skynet.launch(...)
	local addr = c.command("LAUNCH", table.concat({...}," "))
	if addr then
		return tonumber("0x" .. string.sub(addr , 2))
	end
end
```

```c
const char* skynet_command(struct skynet_context* ctx, "LAUNCH", const char* param) {
  return cmd_launch(ctx, param);
}
const char* cmd_launch(skynet_context* ctx, const char* para) {
  // @para: has the form "module_name arguments"
  // 1. create a new context with the module_name and arguments 
  skynet_context* inst = skynet_context_new(module_name, arguments);
  // 2. store its handle to ctx->result array and return the string (hex integer string)
  id_to_hex(context->result, inst->handle);
		return context->result;
}
skynet_context* skynet_context_new(const char* c_service_lib_name, const char* para) {
  //1. load or reuse c dynamic module (ctx->mod)
  //2. create a new module instance (call <module>_create)
  //3. create a new skynet_context (ctx)
  //4. create and register a new handle (ctx->handle)
  //5. create a new message queue (ctx->quque)
  //6. initialise the instance (call <module>_init)
  //7. return the new created skynet_context
}
```

