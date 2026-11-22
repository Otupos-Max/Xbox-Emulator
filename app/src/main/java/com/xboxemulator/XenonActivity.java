package com.xboxemulator;

import android.app.Dialog;
import android.app.NativeActivity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.text.InputFilter;
import android.text.InputType;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/**
 * NativeActivity em tela cheia imersiva: esconde a barra de status e os
 * botões de navegação do Android (voltar, início e abas). Eles só aparecem
 * temporariamente ao deslizar a partir da borda da tela.
 */
public class XenonActivity extends NativeActivity {
    private static final String TAG = "Xenon";
    private Dialog profileDialog;
    private TextView createButton;
    private TextView profileError;
    private EditText nameInput;
    private Dialog searchDialog;

    static {
        // NativeActivity abre a biblioteca com dlopen, o que não registra os
        // métodos "native" do Java. Carregar aqui garante nativeOnProfileCreated.
        System.loadLibrary("xenon_native");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = window.getAttributes();
            lp.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(lp);
        }
        hideSystemBars();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    public void showCreateProfileDialog() {
        runOnUiThread(() -> {
            if (isFinishing() || (profileDialog != null && profileDialog.isShowing())) {
                return;
            }

            int padding = dp(24);
            LinearLayout content = new LinearLayout(this);
            content.setOrientation(LinearLayout.VERTICAL);
            content.setPadding(padding, dp(20), padding, dp(20));
            content.setBackground(roundedBackground(Color.WHITE, dp(20), Color.TRANSPARENT));
            content.setElevation(dp(14));

            TextView eyebrow = new TextView(this);
            eyebrow.setText("XENON  /  NOVO PERFIL");
            eyebrow.setTextSize(12);
            eyebrow.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            eyebrow.setTextColor(Color.rgb(28, 138, 72));
            content.addView(eyebrow);

            TextView title = new TextView(this);
            title.setText("Vamos criar sua conta");
            title.setTextSize(24);
            title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            title.setTextColor(Color.rgb(24, 31, 27));
            LinearLayout.LayoutParams titleParams = wrapParams();
            titleParams.topMargin = dp(2);
            content.addView(title, titleParams);

            TextView subtitle = new TextView(this);
            subtitle.setText("Defina um nome e escolha seu avatar.");
            subtitle.setTextSize(14);
            subtitle.setTextColor(Color.rgb(105, 115, 108));
            LinearLayout.LayoutParams subtitleParams = wrapParams();
            subtitleParams.topMargin = dp(1);
            content.addView(subtitle, subtitleParams);

            LinearLayout formRow = new LinearLayout(this);
            formRow.setOrientation(LinearLayout.HORIZONTAL);
            LinearLayout.LayoutParams formParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            formParams.topMargin = dp(16);
            content.addView(formRow, formParams);

            LinearLayout nameColumn = new LinearLayout(this);
            nameColumn.setOrientation(LinearLayout.VERTICAL);
            formRow.addView(nameColumn, new LinearLayout.LayoutParams(0, -2, 1));
            nameColumn.addView(label("NOME DA CONTA"));

            nameInput = new EditText(this);
            nameInput.setSingleLine(true);
            nameInput.setTextSize(17);
            nameInput.setTextColor(Color.rgb(24, 31, 27));
            nameInput.setHintTextColor(Color.rgb(145, 153, 148));
            nameInput.setHint("Digite seu nome");
            nameInput.setInputType(InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_FLAG_CAP_WORDS
                    | InputType.TYPE_TEXT_VARIATION_PERSON_NAME);
            nameInput.setFilters(new InputFilter[] {new InputFilter.LengthFilter(32)});
            nameInput.setPadding(dp(12), 0, dp(12), 0);
            nameInput.setBackground(roundedBackground(
                    Color.rgb(246, 248, 246), dp(12), Color.rgb(225, 230, 226)));
            LinearLayout.LayoutParams inputParams = new LinearLayout.LayoutParams(-1, dp(56));
            inputParams.topMargin = dp(8);
            nameColumn.addView(nameInput, inputParams);

            LinearLayout genderColumn = new LinearLayout(this);
            genderColumn.setOrientation(LinearLayout.VERTICAL);
            LinearLayout.LayoutParams genderColumnParams =
                    new LinearLayout.LayoutParams(0, -2, 1.15f);
            genderColumnParams.leftMargin = dp(20);
            formRow.addView(genderColumn, genderColumnParams);
            genderColumn.addView(label("ESCOLHA O AVATAR"));

            LinearLayout genderOptions = new LinearLayout(this);
            genderOptions.setOrientation(LinearLayout.HORIZONTAL);
            LinearLayout.LayoutParams optionsParams = new LinearLayout.LayoutParams(-1, dp(84));
            optionsParams.topMargin = dp(8);
            genderColumn.addView(genderOptions, optionsParams);

            LinearLayout maleOption = createGenderOption("M", "Masculino");
            LinearLayout femaleOption = createGenderOption("F", "Feminino");
            boolean[] selectedFemale = {false};
            Runnable updateGenderOptions = () -> {
                styleGenderOption(maleOption, !selectedFemale[0]);
                styleGenderOption(femaleOption, selectedFemale[0]);
            };
            maleOption.setOnClickListener(view -> {
                selectedFemale[0] = false;
                updateGenderOptions.run();
            });
            femaleOption.setOnClickListener(view -> {
                selectedFemale[0] = true;
                updateGenderOptions.run();
            });
            LinearLayout.LayoutParams optionParams =
                    new LinearLayout.LayoutParams(0, -1, 1);
            genderOptions.addView(maleOption, optionParams);
            LinearLayout.LayoutParams secondOptionParams = new LinearLayout.LayoutParams(0, -1, 1);
            secondOptionParams.leftMargin = dp(8);
            genderOptions.addView(femaleOption, secondOptionParams);
            updateGenderOptions.run();

            profileError = new TextView(this);
            profileError.setTextSize(14);
            profileError.setTextColor(Color.rgb(178, 45, 45));
            profileError.setVisibility(View.GONE);
            LinearLayout.LayoutParams errorParams = wrapParams();
            errorParams.topMargin = dp(10);
            content.addView(profileError, errorParams);

            LinearLayout actions = new LinearLayout(this);
            actions.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
            LinearLayout.LayoutParams actionsParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            actionsParams.topMargin = dp(18);
            content.addView(actions, actionsParams);

            TextView cancelButton = actionButton("Cancelar", false);
            cancelButton.setOnClickListener(view -> profileDialog.dismiss());
            actions.addView(cancelButton);
            createButton = actionButton("Criar perfil", true);
            LinearLayout.LayoutParams createParams = wrapParams();
            createParams.leftMargin = dp(10);
            actions.addView(createButton, createParams);
            createButton.setOnClickListener(view -> submitProfile(selectedFemale[0]));

            ScrollView dialogContent = new ScrollView(this);
            dialogContent.setFillViewport(true);
            dialogContent.addView(content, new ScrollView.LayoutParams(
                    ScrollView.LayoutParams.MATCH_PARENT,
                    ScrollView.LayoutParams.WRAP_CONTENT));

            profileDialog = new Dialog(this);
            profileDialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
            profileDialog.setContentView(dialogContent);
            profileDialog.setCancelable(true);
            profileDialog.setOnDismissListener(dialog -> {
                profileDialog = null;
                createButton = null;
                profileError = null;
                nameInput = null;
            });
            profileDialog.show();
            Window dialogWindow = profileDialog.getWindow();
            if (dialogWindow != null) {
                dialogWindow.setBackgroundDrawableResource(android.R.color.transparent);
                dialogWindow.setSoftInputMode(
                        WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
                                | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
                int screenWidth = getResources().getDisplayMetrics().widthPixels;
                int screenHeight = getResources().getDisplayMetrics().heightPixels;
                int dialogWidth = Math.min(dp(760), (int) (screenWidth * 0.94f));
                dialogWindow.setLayout(dialogWidth, WindowManager.LayoutParams.WRAP_CONTENT);
                // Sem altura fixa: o Hub cresce até caber tudo e só rola se
                // passar de 94% da tela, então os botões nunca são cortados.
                int maxHeight = (int) (screenHeight * 0.94f);
                boolean[] limited = {false};
                dialogContent.getViewTreeObserver().addOnGlobalLayoutListener(() -> {
                    if (!limited[0] && dialogContent.getHeight() > maxHeight) {
                        limited[0] = true;
                        dialogWindow.setLayout(dialogWidth, maxHeight);
                    }
                });
                WindowManager.LayoutParams attributes = dialogWindow.getAttributes();
                attributes.dimAmount = 0.62f;
                dialogWindow.setAttributes(attributes);
            }
            nameInput.requestFocus();
        });
    }

    public void onProfileCreationResult(boolean success, String message) {
        runOnUiThread(() -> {
            if (profileDialog == null || !profileDialog.isShowing()) return;
            if (success) {
                profileDialog.dismiss();
                return;
            }
            if (profileError != null) {
                profileError.setText(message);
                profileError.setVisibility(View.VISIBLE);
            }
            if (createButton != null) {
                profileDialog.setCancelable(true);
                createButton.setEnabled(true);
                createButton.setAlpha(1f);
                createButton.setText("Tentar novamente");
            }
        });
    }

    private void submitProfile(boolean female) {
        String name = nameInput.getText().toString().trim();
        if (name.isEmpty()) {
            nameInput.setError("Digite um nome para continuar");
            nameInput.requestFocus();
            return;
        }
        try {
            saveProfile(name, female);
        } catch (IOException | SecurityException error) {
            Log.e(TAG, "Não foi possível salvar o perfil no armazenamento privado", error);
            profileError.setText("Não foi possível salvar o perfil neste dispositivo.");
            profileError.setVisibility(View.VISIBLE);
            return;
        }
        profileError.setVisibility(View.GONE);
        profileDialog.setCancelable(false);
        createButton.setEnabled(false);
        createButton.setAlpha(.65f);
        createButton.setText("Salvando...");
        try {
            if (!nativeOnProfileCreated(name, female)) {
                onProfileCreationResult(false,
                        "Não foi possível iniciar a criação. Tente novamente.");
            }
        } catch (LinkageError | RuntimeException error) {
            Log.e(TAG, "Não foi possível enviar o perfil para o nativo", error);
            profileDialog.setCancelable(true);
            profileError.setText("Falha ao iniciar a criação. Tente novamente.");
            profileError.setVisibility(View.VISIBLE);
            createButton.setEnabled(true);
            createButton.setAlpha(1f);
            createButton.setText("Tentar novamente");
        }
    }

    private void saveProfile(String name, boolean female) throws IOException {
        File destination = new File(getFilesDir(), "xenon-profile.dat");
        File temporary = new File(getFilesDir(), "xenon-profile.dat.tmp");
        String contents = "XENON_PROFILE_V1\n" + (female ? "F" : "M") + "\n" + name + "\n";
        Files.write(temporary.toPath(), contents.getBytes(StandardCharsets.UTF_8));
        try {
            Files.move(temporary.toPath(), destination.toPath(),
                    StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException atomicMoveError) {
            try {
                Files.move(temporary.toPath(), destination.toPath(),
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (IOException moveError) {
                moveError.addSuppressed(atomicMoveError);
                throw moveError;
            }
        } finally {
            Files.deleteIfExists(temporary.toPath());
        }
    }

    private LinearLayout createGenderOption(String marker, String title) {
        LinearLayout option = new LinearLayout(this);
        option.setGravity(Gravity.CENTER);
        option.setOrientation(LinearLayout.VERTICAL);

        TextView icon = new TextView(this);
        icon.setText(marker);
        icon.setTextSize(17);
        icon.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        icon.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams iconParams = new LinearLayout.LayoutParams(dp(32), dp(32));
        option.addView(icon, iconParams);

        TextView titleView = new TextView(this);
        titleView.setText(title);
        titleView.setTextSize(15);
        titleView.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        LinearLayout.LayoutParams titleParams = wrapParams();
        titleParams.topMargin = dp(3);
        option.addView(titleView, titleParams);
        option.setTag(new View[] {icon, titleView});
        option.setFocusable(true);
        option.setClickable(true);
        return option;
    }

    private void styleGenderOption(LinearLayout option, boolean selected) {
        option.setBackground(roundedBackground(
                selected ? Color.rgb(233, 247, 237) : Color.rgb(247, 249, 247),
                dp(15), selected ? Color.rgb(39, 151, 77) : Color.rgb(226, 232, 227)));
        View[] content = (View[]) option.getTag();
        TextView icon = (TextView) content[0];
        icon.setTextColor(selected ? Color.WHITE : Color.rgb(62, 83, 69));
        icon.setBackground(roundedBackground(
                selected ? Color.rgb(38, 148, 75) : Color.rgb(232, 237, 233),
                dp(19), Color.TRANSPARENT));
        ((TextView) content[1]).setTextColor(Color.rgb(31, 43, 35));
        option.setSelected(selected);
        option.setContentDescription(((TextView) content[1]).getText()
                + (selected ? ", selecionado" : ""));
    }

    private TextView label(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(13);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setTextColor(Color.rgb(93, 105, 97));
        return view;
    }

    private TextView actionButton(String text, boolean primary) {
        TextView button = new TextView(this);
        button.setText(text);
        button.setTextSize(16);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setGravity(Gravity.CENTER);
        button.setPadding(dp(24), dp(12), dp(24), dp(12));
        button.setMinHeight(dp(50));
        button.setMinWidth(dp(130));
        button.setBackground(roundedBackground(
                primary ? Color.rgb(31, 145, 70) : Color.rgb(242, 245, 242),
                dp(14), primary ? Color.TRANSPARENT : Color.rgb(230, 234, 230)));
        button.setTextColor(primary ? Color.WHITE : Color.rgb(62, 72, 65));
        button.setFocusable(true);
        button.setClickable(true);
        return button;
    }

    private GradientDrawable roundedBackground(int color, int radius, int strokeColor) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(radius);
        if (strokeColor != Color.TRANSPARENT) {
            background.setStroke(dp(1), strokeColor);
        }
        return background;
    }

    private LinearLayout.LayoutParams wrapParams() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private int dp(float value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private native boolean nativeOnProfileCreated(String name, boolean female);

    /** Envia o termo de busca da Xenon Store para o lado nativo (sem retorno: a
     *  busca roda em segundo plano e o resultado chega via polling). */
    private native void nativeOnStoreSearch(String query);

    /**
     * Abre um dialogo simples de busca para a Xenon Store, pre-preenchido com
     * o termo atual. Ao confirmar, chama de volta nativeOnStoreSearch() (sem
     * retorno: a busca roda em segundo plano no lado nativo).
     */
    public void showSearchDialog(String currentQuery) {
        runOnUiThread(() -> {
            if (isFinishing() || (searchDialog != null && searchDialog.isShowing())) {
                return;
            }
            int padding = dp(22);
            LinearLayout content = new LinearLayout(this);
            content.setOrientation(LinearLayout.VERTICAL);
            content.setPadding(padding, dp(18), padding, dp(18));
            content.setBackground(roundedBackground(Color.WHITE, dp(18), Color.TRANSPARENT));
            content.setElevation(dp(14));

            TextView eyebrow = new TextView(this);
            eyebrow.setText("XENON STORE  /  BUSCAR");
            eyebrow.setTextSize(12);
            eyebrow.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            eyebrow.setTextColor(Color.rgb(28, 138, 72));
            content.addView(eyebrow);

            TextView title = new TextView(this);
            title.setText("Procurar um jogo");
            title.setTextSize(21);
            title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            title.setTextColor(Color.rgb(24, 31, 27));
            LinearLayout.LayoutParams titleParams = wrapParams();
            titleParams.topMargin = dp(2);
            content.addView(title, titleParams);

            EditText searchInput = new EditText(this);
            searchInput.setSingleLine(true);
            searchInput.setTextSize(17);
            searchInput.setTextColor(Color.rgb(24, 31, 27));
            searchInput.setHintTextColor(Color.rgb(145, 153, 148));
            searchInput.setHint("Ex.: forza, portal, minecraft...");
            searchInput.setInputType(InputType.TYPE_CLASS_TEXT);
            searchInput.setFilters(new InputFilter[] {new InputFilter.LengthFilter(64)});
            searchInput.setPadding(dp(12), 0, dp(12), 0);
            searchInput.setBackground(roundedBackground(
                    Color.rgb(246, 248, 246), dp(12), Color.rgb(225, 230, 226)));
            if (currentQuery != null) searchInput.setText(currentQuery);
            searchInput.setSelection(searchInput.getText().length());
            LinearLayout.LayoutParams inputParams = new LinearLayout.LayoutParams(-1, dp(56));
            inputParams.topMargin = dp(16);
            content.addView(searchInput, inputParams);

            LinearLayout actions = new LinearLayout(this);
            actions.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
            LinearLayout.LayoutParams actionsParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            actionsParams.topMargin = dp(18);
            content.addView(actions, actionsParams);

            TextView clearButton = actionButton("Limpar", false);
            clearButton.setOnClickListener(view -> {
                try {
                    nativeOnStoreSearch("");
                } catch (LinkageError | RuntimeException error) {
                    Log.e(TAG, "Não foi possível limpar a busca", error);
                }
                if (searchDialog != null) searchDialog.dismiss();
            });
            actions.addView(clearButton);

            TextView searchButton = actionButton("Buscar", true);
            LinearLayout.LayoutParams searchParams = wrapParams();
            searchParams.leftMargin = dp(10);
            actions.addView(searchButton, searchParams);
            Runnable submit = () -> {
                String query = searchInput.getText().toString().trim();
                try {
                    nativeOnStoreSearch(query);
                } catch (LinkageError | RuntimeException error) {
                    Log.e(TAG, "Não foi possível iniciar a busca", error);
                }
                if (searchDialog != null) searchDialog.dismiss();
            };
            searchButton.setOnClickListener(view -> submit.run());

            searchDialog = new Dialog(this);
            searchDialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
            searchDialog.setContentView(content);
            searchDialog.setCancelable(true);
            searchDialog.setOnDismissListener(dialog -> searchDialog = null);
            searchDialog.show();
            Window dialogWindow = searchDialog.getWindow();
            if (dialogWindow != null) {
                dialogWindow.setBackgroundDrawableResource(android.R.color.transparent);
                int screenWidth = getResources().getDisplayMetrics().widthPixels;
                int dialogWidth = Math.min(dp(520), (int) (screenWidth * 0.9f));
                dialogWindow.setLayout(dialogWidth, WindowManager.LayoutParams.WRAP_CONTENT);
                WindowManager.LayoutParams attributes = dialogWindow.getAttributes();
                attributes.dimAmount = 0.62f;
                dialogWindow.setAttributes(attributes);
            }
            searchInput.requestFocus();
        });
    }

    /**
     * Baixa uma URL via HTTP GET e devolve os bytes crus da resposta, ou null
     * em caso de falha. Chamado pelo codigo nativo a partir de uma thread em
     * segundo plano (nunca a thread principal), entao o bloqueio de rede e
     * seguro aqui.
     */
    public byte[] httpGet(String urlString) {
        HttpURLConnection connection = null;
        try {
            URL url = new URL(urlString);
            connection = (HttpURLConnection) url.openConnection();
            connection.setConnectTimeout(8000);
            connection.setReadTimeout(8000);
            connection.setRequestMethod("GET");
            connection.setRequestProperty("Accept", "application/json, image/*;q=0.9, */*;q=0.8");
            int code = connection.getResponseCode();
            InputStream stream = (code >= 200 && code < 300)
                    ? connection.getInputStream() : connection.getErrorStream();
            if (stream == null) return null;
            ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            byte[] chunk = new byte[8192];
            int read;
            while ((read = stream.read(chunk)) != -1) {
                buffer.write(chunk, 0, read);
            }
            stream.close();
            if (code < 200 || code >= 300) {
                Log.e(TAG, "HTTP " + code + " ao acessar " + urlString);
                return null;
            }
            return buffer.toByteArray();
        } catch (IOException | RuntimeException error) {
            Log.e(TAG, "Falha ao baixar " + urlString, error);
            return null;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    /**
     * Decodifica bytes de imagem (ja baixados, por exemplo pela capa de um
     * jogo) no mesmo formato usado por decodeAssetBitmapRGBA: result[0] =
     * largura, result[1] = altura, result[2..] = pixels ARGB_8888.
     */
    public int[] decodeBytesRGBA(byte[] data) {
        if (data == null || data.length == 0) return null;
        try {
            Bitmap bitmap = BitmapFactory.decodeByteArray(data, 0, data.length);
            if (bitmap == null) return null;
            int width = bitmap.getWidth();
            int height = bitmap.getHeight();
            int[] pixels = new int[width * height];
            bitmap.getPixels(pixels, 0, width, 0, 0, width, height);
            bitmap.recycle();
            int[] result = new int[2 + pixels.length];
            result[0] = width;
            result[1] = height;
            System.arraycopy(pixels, 0, result, 2, pixels.length);
            return result;
        } catch (OutOfMemoryError | RuntimeException error) {
            Log.e(TAG, "Não foi possível decodificar a imagem baixada", error);
            return null;
        }
    }

    /** Abre um link externo (pagina do jogo) no navegador ou app padrao. */
    public void openExternalLink(String url) {
        runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(intent);
            } catch (RuntimeException error) {
                Log.e(TAG, "Não foi possível abrir o link " + url, error);
            }
        });
    }

    /**
     * Chamado pelo codigo nativo (xenon_native.cpp) para carregar a logo do
     * app a partir dos assets, por exemplo "logo/logo.png". Decodifica o PNG
     * com o BitmapFactory e devolve os pixels em formato que o nativo sabe
     * ler: result[0] = largura, result[1] = altura, result[2..] = pixels
     * ARGB_8888 (um int por pixel), linha por linha. Devolve null se o
     * arquivo nao existir em app/src/main/assets ou nao puder ser lido.
     */
    public int[] decodeAssetBitmapRGBA(String assetPath) {
        try (InputStream stream = getAssets().open(assetPath)) {
            Bitmap bitmap = BitmapFactory.decodeStream(stream);
            if (bitmap == null) {
                Log.e(TAG, "Não foi possível decodificar " + assetPath);
                return null;
            }
            int width = bitmap.getWidth();
            int height = bitmap.getHeight();
            int[] pixels = new int[width * height];
            bitmap.getPixels(pixels, 0, width, 0, 0, width, height);
            bitmap.recycle();

            int[] result = new int[2 + pixels.length];
            result[0] = width;
            result[1] = height;
            System.arraycopy(pixels, 0, result, 2, pixels.length);
            return result;
        } catch (IOException | OutOfMemoryError error) {
            Log.e(TAG, "Não foi possível carregar " + assetPath + " dos assets", error);
            return null;
        }
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            View decor = window.getDecorView();
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN);
        }
    }
}
