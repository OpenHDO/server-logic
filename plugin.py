"""Persistent automation rules with extension actions and existing room/device APIs."""
from fastapi import HTTPException
from types import SimpleNamespace
import time

def validate_rules(rules):
    if not isinstance(rules, list) or len(rules) > 500:
        raise HTTPException(422, "Expected up to 500 rules")
    ids = set()
    for rule in rules:
        if not isinstance(rule, dict) or not isinstance(rule.get("id"), str) or rule["id"] in ids:
            raise HTTPException(422, "Every rule needs a unique id")
        ids.add(rule["id"])
        if not isinstance(rule.get("event"), str) or not isinstance(rule.get("action"), dict):
            raise HTTPException(422, "Rule requires an event and action")
        if not isinstance(rule["action"].get("type"), str) or not isinstance(rule["action"].get("config"), dict):
            raise HTTPException(422, "Action requires type and config")
        if not isinstance(rule.get("match", {}), dict) or type(rule.get("cooldown", 1)) not in {int, float} or not 1 <= rule.get("cooldown", 1) <= 86400:
            raise HTTPException(422, "match must be an object; cooldown must be 1–86400 seconds")
    return rules

def activate(ctx):
    last_run = {}
    async def device_action(config, event):
        # Device service validates the capability, value and availability before sending.
        await ctx.service("devices").action(config["device_id"], SimpleNamespace(code=config["code"], value=config["value"]))
    ctx.extension("flowNodes", "device-action", device_action)

    async def execute(rule, event):
        action = ctx.extensions("flowNodes").get(rule["action"]["type"])
        if action is None:
            raise HTTPException(409, "Enable the action's plugin first")
        result = await action(rule["action"]["config"], event)
        return {"id": rule["id"], "result": result}

    async def inventory(request, context):
        return {"rules": ctx.store.get("rules", []), "actions": list(ctx.extensions("flowNodes"))}

    async def save(request, context):
        rules = validate_rules(await request.json())
        ctx.store.set("rules", rules)
        return {"rules": rules}

    async def run(request, context):
        body = await request.json()
        rule = next((rule for rule in ctx.store.get("rules", []) if rule["id"] == body.get("id")), None)
        if rule is None:
            raise HTTPException(404, "Rule not found")
        return await execute(rule, {"type":"manual", "payload":{}})

    async def event_handler(event):
        for rule in ctx.store.get("rules", []):
            if rule.get("enabled", True) and rule["event"] == event["type"]:
                match = rule.get("match", {})
                if all(event["payload"].get(key) == value for key, value in match.items()):
                    if time.monotonic() - last_run.get(rule["id"], float("-inf")) < rule.get("cooldown", 1):
                        continue
                    last_run[rule["id"]] = time.monotonic()
                    try:
                        await execute(rule, event)
                    except Exception:
                        ctx.log.exception("Automation failed: %s", rule["id"])

    ctx.on("device.changed", event_handler)
    ctx.on("devices.updated", event_handler)
    ctx.route("rules", inventory, role="user")
    ctx.route("rules", save, methods=("PUT",))
    ctx.route("run", run, methods=("POST",))
