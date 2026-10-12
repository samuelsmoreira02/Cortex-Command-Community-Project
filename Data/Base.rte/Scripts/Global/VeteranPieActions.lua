-- Pie slice function for the "Veterans" global script. Scripts don't share globals, so the request is left on the actor, and the script picks it up on its next update.

function VeteranShowRoster(pieMenuOwner, pieMenu, pieSlice)
	pieMenuOwner:SetNumberValue("VeteranRosterRequested", 1);
end
