package com.argon.applications;

import net.kyori.adventure.text.Component;
import net.kyori.adventure.text.format.NamedTextColor;
import org.bukkit.*;
import org.bukkit.configuration.file.YamlConfiguration;
import org.bukkit.entity.Player;
import org.bukkit.event.*;
import org.bukkit.event.entity.*;
import org.bukkit.event.player.*;
import org.bukkit.inventory.ItemStack;
import org.bukkit.plugin.java.JavaPlugin;
import org.bukkit.scheduler.BukkitRunnable;

import java.io.File;
import java.io.IOException;
import java.util.*;

public final class ArgonApplications extends JavaPlugin implements Listener {
    private final Map<UUID, String> stages = new HashMap<>();
    private final Map<UUID, String> names = new HashMap<>();
    private final Set<UUID> approved = new HashSet<>();
    private YamlConfiguration data;
    private File dataFile;

    @Override public void onEnable() {
        saveDefaultConfig();
        dataFile = new File(getDataFolder(), "applications.yml");
        if (!dataFile.exists()) try { getDataFolder().mkdirs(); dataFile.createNewFile(); } catch (IOException e) { getLogger().severe(e.getMessage()); }
        data = YamlConfiguration.loadConfiguration(dataFile);
        for (String id : data.getStringList("approved")) try { approved.add(UUID.fromString(id)); } catch (Exception ignored) {}
        getServer().getPluginManager().registerEvents(this, this);
        Objects.requireNonNull(getCommand("app")).setExecutor((sender, command, label, args) -> {
            if (!sender.hasPermission("argonapplications.admin")) { sender.sendMessage("No permission."); return true; }
            if (args.length == 0 || args[0].equalsIgnoreCase("list")) {
                sender.sendMessage("Pending: " + data.getConfigurationSection("pending").getKeys(false)); return true;
            }
            if (args.length < 2) { sender.sendMessage("/app approve|reject <player>"); return true; }
            OfflinePlayer target = Bukkit.getOfflinePlayer(args[1]);
            UUID id = target.getUniqueId();
            if (args[0].equalsIgnoreCase("approve")) {
                approved.add(id); data.set("pending." + id, null); saveData();
                Player online = Bukkit.getPlayer(id);
                if (online != null) release(online);
                sender.sendMessage("Approved " + target.getName());
            } else if (args[0].equalsIgnoreCase("reject")) {
                data.set("pending." + id, null); saveData(); sender.sendMessage("Removed application for " + target.getName());
            } else sender.sendMessage("/app approve|reject <player>");
            return true;
        });
    }
    private void saveData() { data.set("approved", approved.stream().map(UUID::toString).toList()); try { data.save(dataFile); } catch (IOException e) { getLogger().severe(e.getMessage()); } }
    private boolean bypass(Player p) { return p.isOp() || p.hasPermission("argonapplications.bypass"); }
    private boolean pending(Player p) { return !approved.contains(p.getUniqueId()) && !bypass(p); }
    private void begin(Player p) {
        String lobby = getConfig().getString("lobby-world", "application_lobby");
        World w = Bukkit.getWorld(lobby);
        if (w == null) { p.sendMessage("Application lobby world is missing: " + lobby); return; }
        // Save original survival inventory once; never overwrite it with lobby items.
        String key = "pending." + p.getUniqueId();
        if (!data.contains(key + ".saved")) {
            data.set(key + ".saved", p.getInventory().getContents());
            data.set(key + ".armor", p.getInventory().getArmorContents());
            data.set(key + ".offhand", p.getInventory().getItemInOffHand());
            data.set(key + ".gamemode", p.getGameMode().name());
        }
        data.set(key + ".player", p.getName()); data.set(key + ".bedrock", isBedrock(p)); saveData();
        p.getInventory().clear(); p.getInventory().setArmorContents(new ItemStack[4]); p.getInventory().setItemInOffHand(null);
        p.setGameMode(GameMode.ADVENTURE); p.setFoodLevel(20); p.setSaturation(20); p.setHealth(20);
        p.teleport(w.getSpawnLocation()); stages.put(p.getUniqueId(), "name");
        p.sendMessage(Component.text("Welcome! Enter your real first name in chat (not your Minecraft username).", NamedTextColor.YELLOW));
    }
    private boolean isBedrock(Player p) {
        try { Class<?> api=Class.forName("org.geysermc.floodgate.api.FloodgateApi"); Object instance=api.getMethod("getInstance").invoke(null); return (boolean)api.getMethod("isFloodgatePlayer", UUID.class).invoke(instance,p.getUniqueId()); } catch(Exception e){ return false; }
    }
    @EventHandler public void join(PlayerJoinEvent e) { new BukkitRunnable(){public void run(){ Player p=e.getPlayer(); if(!p.isOnline())return; if(approved.contains(p.getUniqueId())||bypass(p)){p.setGameMode(GameMode.SURVIVAL);return;} begin(p); }}.runTaskLater(this,2); }
    @EventHandler public void chat(AsyncPlayerChatEvent e) {
        UUID id=e.getPlayer().getUniqueId(); String stage=stages.get(id); if(stage==null)return; e.setCancelled(true); String msg=e.getMessage().trim();
        Bukkit.getScheduler().runTask(this,()->{
            Player p=e.getPlayer(); if(!p.isOnline())return;
            if(stage.equals("name")) { if(!msg.matches("[A-Za-z][A-Za-z' -]{0,29}")){p.sendMessage("Use a first name (letters, spaces, apostrophes or hyphens; max 30).");return;} names.put(id,msg); stages.put(id,"confirm"); p.sendMessage("You entered: "+msg+". Is this correct? Type yes or no."); }
            else if(stage.equals("confirm")) { if(msg.equalsIgnoreCase("yes")||msg.equalsIgnoreCase("y")){data.set("pending."+id+".firstName",names.get(id));data.set("pending."+id+".submitted",System.currentTimeMillis());saveData();stages.remove(id);p.sendMessage("Application submitted. You can play parkour while staff review it.");}else if(msg.equalsIgnoreCase("no")||msg.equalsIgnoreCase("n")){stages.put(id,"name");p.sendMessage("Enter your first name again.");}else p.sendMessage("Type yes or no.");}
        });
    }
    @EventHandler public void block(BlockBreakEvent e){if(pending(e.getPlayer()))e.setCancelled(true);}
    @EventHandler public void place(BlockPlaceEvent e){if(pending(e.getPlayer()))e.setCancelled(true);}
    @EventHandler public void damage(EntityDamageEvent e){if(e.getEntity() instanceof Player p && pending(p) && e.getCause()==EntityDamageEvent.DamageCause.FALL)e.setCancelled(true);}
    @EventHandler public void combat(EntityDamageByEntityEvent e){if(e.getDamager() instanceof Player p&&pending(p)||e.getEntity() instanceof Player p&&pending(p))e.setCancelled(true);}
    @EventHandler public void hunger(FoodLevelChangeEvent e){if(e.getEntity() instanceof Player p&&pending(p))e.setCancelled(true);}
    @EventHandler public void drop(PlayerDropItemEvent e){if(pending(e.getPlayer()))e.setCancelled(true);}
    @EventHandler public void pickup(EntityPickupItemEvent e){if(e.getEntity() instanceof Player p&&pending(p))e.setCancelled(true);}
    @EventHandler public void command(PlayerCommandPreprocessEvent e){if(pending(e.getPlayer())&&!e.getMessage().toLowerCase().startsWith("/msg ")&&!e.getMessage().toLowerCase().startsWith("/tell ")&&!e.getMessage().equalsIgnoreCase("/help"))e.setCancelled(true);}
    private void release(Player p) {
        String key="pending."+p.getUniqueId(); Object inv=data.get(key+".saved"); if(inv instanceof ItemStack[] items)p.getInventory().setContents(items);
        Object armor=data.get(key+".armor");if(armor instanceof ItemStack[] items)p.getInventory().setArmorContents(items);
        ItemStack off=data.getItemStack(key+".offhand");p.getInventory().setItemInOffHand(off);
        String mode=data.getString(key+".gamemode","SURVIVAL");try{p.setGameMode(GameMode.valueOf(mode));}catch(Exception ignored){p.setGameMode(GameMode.SURVIVAL);}
        World w=Bukkit.getWorld(getConfig().getString("survival-world","jawaaa"));if(w!=null)p.teleport(w.getSpawnLocation());
        stages.remove(p.getUniqueId());p.sendMessage("Application approved. Welcome to survival!");
    }
}