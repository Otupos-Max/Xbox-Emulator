package com.xboxemulator;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class XenonDownloadService extends Service {
    static final String ACTION_START = "com.xboxemulator.download.START";
    static final String ACTION_RESTORE = "com.xboxemulator.download.RESTORE";
    private static final String ACTION_TOGGLE = "com.xboxemulator.download.TOGGLE";
    private static final String ACTION_CANCEL = "com.xboxemulator.download.CANCEL";
    static final String EXTRA_URL = "url";
    static final String EXTRA_TITLE = "title";
    static final String EXTRA_SHA256 = "sha256";
    static final String EXTRA_GAME_ID = "game_id";
    static final String EXTRA_COVER_URL = "cover_url";
    private static final String EXTRA_ID = "download_id";
    private static final String CHANNEL_ID = "xenon_downloads";
    private static final String PREFS = "xenon_downloads";
    private static final String PREF_JOBS = "jobs";
    private static final String STATE_QUEUED = "queued";
    private static final String STATE_RUNNING = "running";
    private static final String STATE_PAUSED = "paused";
    private static final String STATE_FAILED = "failed";
    private static final String TAG = "XenonDownload";
    private static final int MAX_CONCURRENT_DOWNLOADS = 3;
    private static final int DOWNLOAD_BUFFER_SIZE = 1024 * 1024;
    private static final long REQUIRED_FREE_SPACE_MARGIN = 512L * 1024 * 1024;

    private final Object jobsLock = new Object();
    private final Map<String, Download> jobs = new HashMap<>();
    private final ExecutorService workers = Executors.newFixedThreadPool(MAX_CONCURRENT_DOWNLOADS);
    private NotificationManager notificationManager;
    private boolean loaded;
    private int foregroundNotificationId = -1;

    private static final class Download {
        String id;
        String url;
        String title;
        String sha256;
        String gameId;
        String coverUrl;
        String fileName = "";
        String contentType = "";
        String state = STATE_QUEUED;
        long downloaded;
        long total = -1;
        int notificationId;
        volatile boolean active;
        volatile boolean paused;
        volatile boolean cancelled;
        volatile long actionVersion;
        volatile HttpURLConnection connection;

        File partialFile(File gamesDir) {
            return new File(gamesDir, ".xenon-" + id + ".part");
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        notificationManager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        ensureLoaded();
        String action = intent == null ? ACTION_RESTORE : intent.getAction();
        if (ACTION_START.equals(action)) {
            startDownload(intent);
        } else if (ACTION_TOGGLE.equals(action)) {
            toggleDownload(intent.getStringExtra(EXTRA_ID));
        } else if (ACTION_CANCEL.equals(action)) {
            cancelDownload(intent.getStringExtra(EXTRA_ID));
        } else {
            restoreDownloads();
        }
        boolean noJobs;
        synchronized (jobsLock) {
            noJobs = jobs.isEmpty();
        }
        if (noJobs) {
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf(startId);
            return START_NOT_STICKY;
        }
        return START_STICKY;
    }

    private void startDownload(Intent intent) {
        String url = intent.getStringExtra(EXTRA_URL);
        if (url == null || url.trim().isEmpty()) return;
        String title = intent.getStringExtra(EXTRA_TITLE);
        if (title == null || title.trim().isEmpty()) title = "jogo";
        String sha256 = intent.getStringExtra(EXTRA_SHA256);
        String gameId = intent.getStringExtra(EXTRA_GAME_ID);
        String coverUrl = intent.getStringExtra(EXTRA_COVER_URL);
        String id = UUID.nameUUIDFromBytes((url + "\n" + title).getBytes(java.nio.charset.StandardCharsets.UTF_8)).toString();
        Download job;
        synchronized (jobsLock) {
            job = jobs.get(id);
            if (job == null) {
                job = new Download();
                job.id = id;
                job.notificationId = id.hashCode() & 0x7fffffff;
                if (job.notificationId == 0) job.notificationId = 1;
                jobs.put(id, job);
            }
            job.url = url;
            job.title = title;
            job.sha256 = sha256 == null ? "" : sha256.trim();
            job.gameId = gameId == null ? "" : gameId;
            job.coverUrl = coverUrl == null ? "" : coverUrl;
            job.paused = false;
            job.cancelled = false;
            if (!job.active) job.state = STATE_QUEUED;
            saveJobsLocked();
        }
        refreshNotifications();
        launch(job);
    }

    private void restoreDownloads() {
        List<Download> toStart = new ArrayList<>();
        synchronized (jobsLock) {
            for (Download job : jobs.values()) {
                if (STATE_RUNNING.equals(job.state) || STATE_QUEUED.equals(job.state)) {
                    job.state = STATE_QUEUED;
                    job.paused = false;
                    toStart.add(job);
                }
            }
            saveJobsLocked();
        }
        refreshNotifications();
        for (Download job : toStart) launch(job);
    }

    private void toggleDownload(String id) {
        Download job;
        synchronized (jobsLock) {
            job = jobs.get(id);
            if (job == null) return;
            if (job.paused || STATE_FAILED.equals(job.state)) {
                job.paused = false;
                job.cancelled = false;
                job.actionVersion++;
                job.state = STATE_QUEUED;
            } else {
                job.paused = true;
                job.actionVersion++;
                job.state = STATE_PAUSED;
                closeConnection(job);
            }
            saveJobsLocked();
        }
        refreshNotifications();
        if (!job.paused) launch(job);
    }

    private void cancelDownload(String id) {
        Download job;
        synchronized (jobsLock) {
            job = jobs.remove(id);
            if (job == null) return;
            job.cancelled = true;
            job.actionVersion++;
            closeConnection(job);
            saveJobsLocked();
        }
        File gamesDir = new File(getFilesDir(), "games");
        if (!job.fileName.isEmpty()) job.partialFile(gamesDir).delete();
        notificationManager.cancel(job.notificationId);
        publishProgress(job, 3);
        refreshNotifications();
    }

    private void launch(Download job) {
        synchronized (jobsLock) {
            if (job.active || job.paused || job.cancelled || !jobs.containsKey(job.id)) return;
            job.active = true;
            job.state = STATE_RUNNING;
            saveJobsLocked();
        }
        refreshNotifications();
        workers.execute(() -> runDownload(job));
    }

    private void runDownload(Download job) {
        String failure = null;
        try {
            File gamesDir = new File(getFilesDir(), "games");
            if (!gamesDir.exists() && !gamesDir.mkdirs()) throw new IOException("Não foi possível criar a pasta games");
            if (job.fileName.isEmpty()) {
                job.fileName = XenonActivity.suggestedFileName(null, job.url, job.title);
                persistAndNotify(job);
            }
            File partial = job.partialFile(gamesDir);
            while (!job.cancelled && !job.paused) {
                long version = job.actionVersion;
                try {
                    transfer(job, partial);
                    break;
                } catch (IOException error) {
                    if (job.cancelled || job.paused) break;
                    if (job.actionVersion != version) continue;
                    throw error;
                }
            }
            if (job.cancelled || job.paused) return;
            if (!partial.isFile() || partial.length() == 0) throw new IOException("O servidor entregou um arquivo vazio");
            job.downloaded = partial.length();
            if (job.total > 0 && job.downloaded > job.total) {
                partial.delete();
                job.downloaded = 0;
                throw new IOException("O tamanho baixado excedeu o total informado pelo servidor");
            }
            if (job.total > 0 && job.downloaded != job.total) {
                throw new IOException("Download incompleto: " + formatSize(job.downloaded) + " de " + formatSize(job.total));
            }
            if (isSha256(job.sha256) && !job.sha256.equalsIgnoreCase(sha256(partial))) {
                partial.delete();
                job.downloaded = 0;
                throw new IOException("A validação SHA-256 falhou; tente baixar novamente");
            }
                    Map<String, String> existingIsos = isoSnapshot(gamesDir);
                    int extracted = XenonActivity.prepareDownloadedGame(partial, gamesDir, job.fileName,
                    job.contentType, () -> job.cancelled);
                    if (!job.cancelled) saveCoverForNewIsos(job, gamesDir, existingIsos);
            Log.i(TAG, "Download validado para " + job.title + " (" + extracted + " ISO(s))");
            synchronized (jobsLock) {
                jobs.remove(job.id);
                saveJobsLocked();
            }
            publishProgress(job, 3);
            notificationManager.cancel(job.notificationId);
        } catch (Exception error) {
            if (!job.cancelled && !job.paused) {
                failure = error.getMessage() == null ? "Falha de conexão ou resposta inválida" : error.getMessage();
                Log.e(TAG, "Falha ao baixar " + job.title, error);
            }
        } finally {
            job.connection = null;
            job.active = false;
            boolean retained = false;
            synchronized (jobsLock) {
                if (jobs.containsKey(job.id)) {
                    if (job.cancelled) {
                        jobs.remove(job.id);
                    } else if (job.paused) {
                        job.state = STATE_PAUSED;
                    } else if (failure != null) {
                        job.state = STATE_FAILED;
                    }
                    saveJobsLocked();
                    retained = jobs.containsKey(job.id);
                }
            }
            if (retained) updateNotification(job);
            refreshNotifications();
        }
    }

    private void transfer(Download job, File partial) throws IOException {
        long offset = partial.isFile() ? partial.length() : 0;
        HttpURLConnection connection = (HttpURLConnection) new URL(job.url).openConnection();
        job.connection = connection;
        connection.setInstanceFollowRedirects(true);
        connection.setConnectTimeout(15000);
        connection.setReadTimeout(120000);
        connection.setRequestProperty("Accept", "*/*");
        connection.setRequestProperty("Accept-Encoding", "identity");
        connection.setRequestProperty("User-Agent", "Xenon-Emulator/1.0");
        if (offset > 0) connection.setRequestProperty("Range", "bytes=" + offset + "-");
        try {
            connection.connect();
            int status = connection.getResponseCode();
            if (status == 416 && offset > 0) {
                long rangeTotal = contentRangeTotal(connection.getHeaderField("Content-Range"));
                if (rangeTotal == offset) {
                    job.total = rangeTotal;
                    job.downloaded = offset;
                    return;
                }
                if (rangeTotal > 0) job.total = rangeTotal;
                partial.delete();
                job.downloaded = 0;
                throw new IOException("Servidor rejeitou a retomada; tente novamente do início");
            }
            if (offset > 0 && status == HttpURLConnection.HTTP_OK) {
                offset = 0;
            } else if (status < 200 || status >= 300) {
                throw new IOException("Servidor respondeu HTTP " + status);
            }
            if (status == HttpURLConnection.HTTP_PARTIAL) {
                long rangeStart = contentRangeStart(connection.getHeaderField("Content-Range"));
                if (rangeStart != offset) throw new IOException("Resposta HTTP Range inválida");
                long rangeTotal = contentRangeTotal(connection.getHeaderField("Content-Range"));
                if (rangeTotal > 0) job.total = rangeTotal;
            } else if (status != HttpURLConnection.HTTP_OK) {
                throw new IOException("Resposta HTTP inválida: " + status);
            }
            job.contentType = connection.getContentType() == null ? "" : connection.getContentType().toLowerCase(Locale.ROOT);
            long responseLength = connection.getContentLengthLong();
            if (status == HttpURLConnection.HTTP_OK) {
                job.total = responseLength > 0 ? responseLength : -1;
            } else if (job.total <= 0 && responseLength > 0) {
                job.total = offset + responseLength;
            }
            job.downloaded = offset;
            persistAndNotify(job);
            if (job.total > 0) {
                long remaining = Math.max(0, job.total - offset);
                File gamesDir = partial.getParentFile();
                long usable = gamesDir == null ? -1 : gamesDir.getUsableSpace();
                if (usable >= 0 && usable < remaining + REQUIRED_FREE_SPACE_MARGIN) {
                    throw new IOException("Espaço insuficiente: necessário "
                            + formatSize(remaining + REQUIRED_FREE_SPACE_MARGIN)
                            + " livre");
                }
            }
            try (InputStream input = new BufferedInputStream(connection.getInputStream(), DOWNLOAD_BUFFER_SIZE);
                 BufferedOutputStream output = new BufferedOutputStream(
                         new FileOutputStream(partial, offset > 0), DOWNLOAD_BUFFER_SIZE)) {
                byte[] buffer = new byte[DOWNLOAD_BUFFER_SIZE];
                int count;
                long current = offset;
                long lastPersist = System.currentTimeMillis();
                while ((count = input.read(buffer)) != -1) {
                    if (job.cancelled || job.paused) throw new IOException("Download interrompido");
                    output.write(buffer, 0, count);
                    current += count;
                    job.downloaded = current;
                    long now = System.currentTimeMillis();
                    if (now - lastPersist >= 500) {
                        persistAndNotify(job);
                        lastPersist = now;
                    }
                }
                output.flush();
                job.downloaded = partial.length();
            }
            persistAndNotify(job);
        } finally {
            connection.disconnect();
            if (job.connection == connection) job.connection = null;
        }
    }

    private Map<String, String> isoSnapshot(File gamesDir) {
        Map<String, String> snapshot = new HashMap<>();
        File[] files = gamesDir.listFiles();
        if (files == null) return snapshot;
        for (File file : files) {
            if (file.isFile() && file.getName().toLowerCase(Locale.ROOT).endsWith(".iso")) {
                snapshot.put(file.getName(), file.length() + ":" + file.lastModified());
            }
        }
        return snapshot;
    }

    private void saveCoverForNewIsos(Download job, File gamesDir, Map<String, String> existingIsos) {
        if (job.coverUrl.isEmpty() || job.cancelled) return;
        File temporary = new File(gamesDir, ".xenon-cover-" + job.id + ".part");
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(job.coverUrl).openConnection();
            connection.setConnectTimeout(10000);
            connection.setReadTimeout(15000);
            connection.setRequestProperty("Accept", "image/*");
            connection.setRequestProperty("User-Agent", "Xenon-Emulator/1.0");
            int status = connection.getResponseCode();
            if (status < 200 || status >= 300) throw new IOException("HTTP " + status);
            long total = 0;
            try (InputStream input = new BufferedInputStream(connection.getInputStream());
                 BufferedOutputStream output = new BufferedOutputStream(new FileOutputStream(temporary))) {
                byte[] buffer = new byte[16 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    if (job.cancelled) return;
                    total += count;
                    if (total > 8L * 1024 * 1024) throw new IOException("Capa excede 8 MiB");
                    output.write(buffer, 0, count);
                }
            }
            if (total == 0) return;
            File[] files = gamesDir.listFiles();
            if (files == null) return;
            for (File iso : files) {
                if (!iso.isFile() || !iso.getName().toLowerCase(Locale.ROOT).endsWith(".iso")) continue;
                String previous = existingIsos.get(iso.getName());
                String current = iso.length() + ":" + iso.lastModified();
                if (current.equals(previous)) continue;
                if (job.cancelled) return;
                Files.copy(temporary.toPath(), new File(gamesDir, iso.getName() + ".cover").toPath(),
                        StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (Exception error) {
            Log.w(TAG, "Não foi possível salvar a capa de " + job.title, error);
        } finally {
            if (connection != null) connection.disconnect();
            temporary.delete();
        }
    }

    private long contentRangeStart(String value) throws IOException {
        try {
            if (value == null || !value.startsWith("bytes ")) throw new NumberFormatException();
            return Long.parseLong(value.substring(6, value.indexOf('-')));
        } catch (RuntimeException error) {
            throw new IOException("Cabeçalho Content-Range inválido", error);
        }
    }

    private long contentRangeTotal(String value) {
        if (value == null) return -1;
        int slash = value.indexOf('/');
        if (slash < 0 || slash + 1 >= value.length() || value.charAt(slash + 1) == '*') return -1;
        try {
            return Long.parseLong(value.substring(slash + 1));
        } catch (NumberFormatException ignored) {
            return -1;
        }
    }

    private String sha256(File file) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (InputStream input = new BufferedInputStream(new FileInputStream(file))) {
                byte[] buffer = new byte[256 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
            }
            StringBuilder result = new StringBuilder(64);
            for (byte value : digest.digest()) result.append(String.format(Locale.ROOT, "%02x", value & 0xff));
            return result.toString();
        } catch (NoSuchAlgorithmException error) {
            throw new IOException("SHA-256 indisponível", error);
        }
    }

    private boolean isSha256(String value) {
        return value != null && value.matches("(?i)[0-9a-f]{64}");
    }

    private void closeConnection(Download job) {
        HttpURLConnection connection = job.connection;
        if (connection != null) connection.disconnect();
    }

    private void persistAndNotify(Download job) {
        synchronized (jobsLock) {
            if (!jobs.containsKey(job.id)) return;
            saveJobsLocked();
        }
        updateNotification(job);
    }

    private void ensureLoaded() {
        if (loaded) return;
        loaded = true;
        JSONArray saved;
        try {
            saved = new JSONArray(getSharedPreferences(PREFS, MODE_PRIVATE).getString(PREF_JOBS, "[]"));
        } catch (JSONException error) {
            Log.e(TAG, "Estado de downloads inválido", error);
            return;
        }
        synchronized (jobsLock) {
            for (int i = 0; i < saved.length(); i++) {
                JSONObject item = saved.optJSONObject(i);
                if (item == null) continue;
                Download job = new Download();
                job.id = item.optString("id", "");
                job.url = item.optString("url", "");
                job.title = item.optString("title", "jogo");
                job.sha256 = item.optString("sha256", "");
                job.gameId = item.optString("gameId", "");
                job.coverUrl = item.optString("coverUrl", "");
                job.fileName = item.optString("fileName", "");
                job.contentType = item.optString("contentType", "");
                job.state = item.optString("state", STATE_QUEUED);
                job.downloaded = item.optLong("downloaded", 0);
                job.total = item.optLong("total", -1);
                job.notificationId = item.optInt("notificationId", 1);
                if (!job.id.isEmpty() && !job.url.isEmpty()) jobs.put(job.id, job);
            }
        }
    }

    private void saveJobsLocked() {
        JSONArray saved = new JSONArray();
        for (Download job : jobs.values()) {
            JSONObject item = new JSONObject();
            try {
                item.put("id", job.id);
                item.put("url", job.url);
                item.put("title", job.title);
                item.put("sha256", job.sha256);
                item.put("gameId", job.gameId);
                item.put("coverUrl", job.coverUrl);
                item.put("fileName", job.fileName);
                item.put("contentType", job.contentType);
                item.put("state", job.state);
                item.put("downloaded", job.downloaded);
                item.put("total", job.total);
                item.put("notificationId", job.notificationId);
                saved.put(item);
            } catch (JSONException error) {
                Log.e(TAG, "Não foi possível salvar o estado do download", error);
            }
        }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(PREF_JOBS, saved.toString()).apply();
    }

    private void refreshNotifications() {
        List<Download> snapshot;
        synchronized (jobsLock) {
            snapshot = new ArrayList<>(jobs.values());
        }
        Download foreground = null;
        for (Download job : snapshot) {
            updateNotification(job);
            if (foreground == null && !STATE_FAILED.equals(job.state)) foreground = job;
        }
        if (foreground != null) {
            startForeground(foreground.notificationId, buildNotification(foreground));
            foregroundNotificationId = foreground.notificationId;
        } else if (foregroundNotificationId != -1) {
            stopForeground(STOP_FOREGROUND_DETACH);
            foregroundNotificationId = -1;
        }
    }

    private void updateNotification(Download job) {
        if (notificationManager != null && !job.cancelled) {
            notificationManager.notify(job.notificationId, buildNotification(job));
        }
        int state = STATE_PAUSED.equals(job.state) ? 1 : (STATE_FAILED.equals(job.state) ? 2 : 0);
        publishProgress(job, state);
    }

    private void publishProgress(Download job, int state) {
        if (!job.gameId.isEmpty()) {
            XenonActivity.nativeOnStoreDownloadProgress(job.gameId, job.downloaded, job.total, state);
        }
    }

    private Notification buildNotification(Download job) {
        String stateText = STATE_PAUSED.equals(job.state) ? "Pausado"
                : (STATE_FAILED.equals(job.state) ? "Falha no download" : "Baixando");
        String progressText;
        int progress = 0;
        if (job.total > 0) {
            double percent = Math.min(100.0, job.downloaded * 100.0 / job.total);
            progress = (int) Math.min(10000, job.downloaded * 10000.0 / job.total);
            progressText = String.format(Locale.getDefault(), "%.2f%% · %s de %s",
                    percent, formatSize(job.downloaded), formatSize(job.total));
        } else {
            progressText = formatSize(job.downloaded) + " baixados";
        }
        Intent launchIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent contentIntent = launchIntent == null ? null : PendingIntent.getActivity(this,
                job.notificationId, launchIntent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        PendingIntent toggleIntent = serviceAction(ACTION_TOGGLE, job);
        PendingIntent cancelIntent = serviceAction(ACTION_CANCEL, job);
        Notification.Builder builder = new Notification.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setContentTitle(job.title)
                .setContentText(stateText + " · " + progressText)
                .setStyle(new Notification.BigTextStyle().bigText(stateText + " · " + progressText))
                .setOnlyAlertOnce(true)
                .setOngoing(!STATE_FAILED.equals(job.state))
                .setProgress(10000, progress, job.total <= 0);
        if (contentIntent != null) builder.setContentIntent(contentIntent);
        builder.addAction(android.R.drawable.ic_media_pause, "PAUSAR", toggleIntent);
        builder.addAction(android.R.drawable.ic_menu_close_clear_cancel, "CANCELAR", cancelIntent);
        return builder.build();
    }

    private PendingIntent serviceAction(String action, Download job) {
        Intent intent = new Intent(this, XenonDownloadService.class)
                .setAction(action).putExtra(EXTRA_ID, job.id);
        return PendingIntent.getService(this, job.notificationId + (ACTION_CANCEL.equals(action) ? 1 : 0),
                intent, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "Downloads", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Progresso dos downloads da Xenon Store");
            notificationManager.createNotificationChannel(channel);
        }
    }

    private String formatSize(long bytes) {
        if (bytes < 1024) return bytes + " B";
        double value = bytes;
        String[] units = {"B", "KB", "MB", "GB", "TB"};
        int unit = 0;
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024;
            unit++;
        }
        return String.format(Locale.getDefault(), "%.2f %s", value, units[unit]);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        workers.shutdownNow();
        super.onDestroy();
    }
}