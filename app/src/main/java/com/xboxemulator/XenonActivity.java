package com.xboxemulator;

import android.app.Dialog;
import android.app.NativeActivity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
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
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

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
            content.setPadding(padding, padding, padding, dp(20));
            content.setBackground(roundedBackground(Color.WHITE, dp(24), Color.TRANSPARENT));
            content.setElevation(dp(18));

            TextView eyebrow = new TextView(this);
            eyebrow.setText("XENON  /  CONTA");
            eyebrow.setTextSize(11);
            eyebrow.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            eyebrow.setTextColor(Color.rgb(28, 138, 72));
            content.addView(eyebrow);

            TextView title = new TextView(this);
            title.setText("Crie seu perfil");
            title.setTextSize(26);
            title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            title.setTextColor(Color.rgb(24, 31, 27));
            LinearLayout.LayoutParams titleParams = wrapParams();
            titleParams.topMargin = dp(8);
            content.addView(title, titleParams);

            TextView subtitle = new TextView(this);
            subtitle.setText("Escolha um nome e personalize seu avatar.");
            subtitle.setTextSize(14);
            subtitle.setTextColor(Color.rgb(105, 115, 108));
            LinearLayout.LayoutParams subtitleParams = wrapParams();
            subtitleParams.topMargin = dp(5);
            content.addView(subtitle, subtitleParams);

            TextView nameLabel = label("NOME DA CONTA");
            LinearLayout.LayoutParams labelParams = wrapParams();
            labelParams.topMargin = dp(24);
            content.addView(nameLabel, labelParams);

            nameInput = new EditText(this);
            nameInput.setSingleLine(true);
            nameInput.setTextSize(16);
            nameInput.setTextColor(Color.rgb(24, 31, 27));
            nameInput.setHintTextColor(Color.rgb(145, 153, 148));
            nameInput.setHint("Como devemos chamar você?");
            nameInput.setInputType(InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_FLAG_CAP_WORDS
                    | InputType.TYPE_TEXT_VARIATION_PERSON_NAME);
            nameInput.setFilters(new InputFilter[] {new InputFilter.LengthFilter(32)});
            nameInput.setPadding(dp(16), 0, dp(16), 0);
            nameInput.setBackground(roundedBackground(
                    Color.rgb(246, 248, 246), dp(14), Color.rgb(225, 230, 226)));
            LinearLayout.LayoutParams inputParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(54));
            inputParams.topMargin = dp(9);
            content.addView(nameInput, inputParams);

            TextView genderLabel = label("AVATAR");
            LinearLayout.LayoutParams genderLabelParams = wrapParams();
            genderLabelParams.topMargin = dp(22);
            content.addView(genderLabel, genderLabelParams);

            LinearLayout genderOptions = new LinearLayout(this);
            genderOptions.setOrientation(LinearLayout.HORIZONTAL);
            LinearLayout.LayoutParams optionsParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dp(82));
            optionsParams.topMargin = dp(9);
            content.addView(genderOptions, optionsParams);

            LinearLayout maleOption = createGenderOption("M", "Masculino", "Avatar masculino");
            LinearLayout femaleOption = createGenderOption("F", "Feminino", "Avatar feminino");
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
            secondOptionParams.leftMargin = dp(12);
            genderOptions.addView(femaleOption, secondOptionParams);
            updateGenderOptions.run();

            profileError = new TextView(this);
            profileError.setTextSize(13);
            profileError.setTextColor(Color.rgb(178, 45, 45));
            profileError.setVisibility(View.GONE);
            LinearLayout.LayoutParams errorParams = wrapParams();
            errorParams.topMargin = dp(10);
            content.addView(profileError, errorParams);

            LinearLayout actions = new LinearLayout(this);
            actions.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
            LinearLayout.LayoutParams actionsParams = wrapParams();
            actionsParams.topMargin = dp(20);
            content.addView(actions, actionsParams);

            TextView cancelButton = actionButton("Cancelar", false);
            cancelButton.setOnClickListener(view -> profileDialog.dismiss());
            actions.addView(cancelButton);
            createButton = actionButton("Criar perfil", true);
            LinearLayout.LayoutParams createParams = wrapParams();
            createParams.leftMargin = dp(10);
            actions.addView(createButton, createParams);
            createButton.setOnClickListener(view -> submitProfile(selectedFemale[0]));

            ScrollView scrollContent = new ScrollView(this);
            scrollContent.setFillViewport(true);
            scrollContent.setClipToPadding(false);
            scrollContent.addView(content);

            profileDialog = new Dialog(this);
            profileDialog.setContentView(scrollContent);
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
                        WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
                int screenWidth = getResources().getDisplayMetrics().widthPixels;
                int screenHeight = getResources().getDisplayMetrics().heightPixels;
                int dialogWidth = Math.min(dp(560), (int) (screenWidth * 0.90f));
                int dialogHeight = Math.min(dp(520), (int) (screenHeight * 0.88f));
                dialogWindow.setLayout(dialogWidth, dialogHeight);
                WindowManager.LayoutParams attributes = dialogWindow.getAttributes();
                attributes.dimAmount = 0.62f;
                dialogWindow.setAttributes(attributes);
            }
            EditText profileNameInput = nameInput;
            profileNameInput.post(() -> {
                if (profileDialog == null || !profileDialog.isShowing()) return;
                profileNameInput.requestFocus();
                InputMethodManager inputManager =
                        (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
                if (inputManager != null) {
                    inputManager.showSoftInput(
                            profileNameInput, InputMethodManager.SHOW_IMPLICIT);
                }
            });
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

    private LinearLayout createGenderOption(String marker, String title, String subtitle) {
        LinearLayout option = new LinearLayout(this);
        option.setGravity(Gravity.CENTER_VERTICAL);
        option.setOrientation(LinearLayout.HORIZONTAL);
        option.setPadding(dp(12), dp(10), dp(8), dp(10));

        TextView icon = new TextView(this);
        icon.setText(marker);
        icon.setTextSize(17);
        icon.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        icon.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams iconParams = new LinearLayout.LayoutParams(dp(38), dp(38));
        option.addView(icon, iconParams);

        LinearLayout labels = new LinearLayout(this);
        labels.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams labelsParams = new LinearLayout.LayoutParams(0, -2, 1);
        labelsParams.leftMargin = dp(10);
        option.addView(labels, labelsParams);

        TextView titleView = new TextView(this);
        titleView.setText(title);
        titleView.setTextSize(14);
        titleView.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        labels.addView(titleView);

        TextView subtitleView = new TextView(this);
        subtitleView.setText(subtitle);
        subtitleView.setTextSize(11);
        LinearLayout.LayoutParams subtitleParams = wrapParams();
        subtitleParams.topMargin = dp(3);
        labels.addView(subtitleView, subtitleParams);

        option.setTag(new View[] {icon, titleView, subtitleView});
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
        ((TextView) content[2]).setTextColor(Color.rgb(105, 117, 108));
        option.setSelected(selected);
        option.setContentDescription(((TextView) content[1]).getText()
                + (selected ? ", selecionado" : ""));
    }

    private TextView label(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(11);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setTextColor(Color.rgb(93, 105, 97));
        return view;
    }

    private TextView actionButton(String text, boolean primary) {
        TextView button = new TextView(this);
        button.setText(text);
        button.setTextSize(14);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setGravity(Gravity.CENTER);
        button.setPadding(dp(18), 0, dp(18), 0);
        button.setMinHeight(dp(48));
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
