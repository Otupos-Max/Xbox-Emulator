package com.xboxemulator;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.text.InputFilter;
import android.text.InputType;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;

/**
 * NativeActivity em tela cheia imersiva: esconde a barra de status e os
 * botões de navegação do Android (voltar, início e abas). Eles só aparecem
 * temporariamente ao deslizar a partir da borda da tela.
 */
public class XenonActivity extends NativeActivity {
    private static final String TAG = "Xenon";
    private AlertDialog profileDialog;

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

            int padding = (int) (24 * getResources().getDisplayMetrics().density);
            LinearLayout content = new LinearLayout(this);
            content.setOrientation(LinearLayout.VERTICAL);
            content.setPadding(padding, padding, padding, padding / 2);

            TextView nameLabel = new TextView(this);
            nameLabel.setText("Nome da conta");
            nameLabel.setTextSize(15);
            nameLabel.setTextColor(Color.rgb(65, 75, 70));
            content.addView(nameLabel);

            EditText nameInput = new EditText(this);
            nameInput.setSingleLine(true);
            nameInput.setHint("Digite o nome");
            nameInput.setInputType(InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_FLAG_CAP_WORDS
                    | InputType.TYPE_TEXT_VARIATION_PERSON_NAME);
            nameInput.setFilters(new InputFilter[] {new InputFilter.LengthFilter(32)});
            nameInput.setPadding(padding / 2, 0, padding / 2, 0);
            content.addView(nameInput);

            TextView genderLabel = new TextView(this);
            genderLabel.setText("Escolha o gênero do perfil");
            genderLabel.setTextSize(15);
            genderLabel.setTextColor(Color.rgb(65, 75, 70));
            LinearLayout.LayoutParams genderParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            genderParams.topMargin = padding;
            content.addView(genderLabel, genderParams);

            LinearLayout genderOptions = new LinearLayout(this);
            genderOptions.setOrientation(LinearLayout.HORIZONTAL);
            LinearLayout.LayoutParams optionsParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            optionsParams.topMargin = padding / 2;
            content.addView(genderOptions, optionsParams);

            TextView maleOption = createGenderOption(
                    "Masculino", "Modelo masculino", padding);
            TextView femaleOption = createGenderOption(
                    "Feminino", "Modelo feminino", padding);
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
            LinearLayout.LayoutParams optionParams = new LinearLayout.LayoutParams(
                    0, (int) (88 * getResources().getDisplayMetrics().density), 1);
            genderOptions.addView(maleOption, optionParams);
            LinearLayout.LayoutParams secondOptionParams = new LinearLayout.LayoutParams(
                    0, optionParams.height, 1);
            secondOptionParams.leftMargin = padding / 2;
            genderOptions.addView(femaleOption, secondOptionParams);
            updateGenderOptions.run();

            profileDialog = new AlertDialog.Builder(this)
                    .setTitle("Criar perfil")
                    .setView(content)
                    .setNegativeButton("Cancelar", null)
                    .setPositiveButton("Criar", null)
                    .create();
            profileDialog.setOnShowListener(dialog -> {
                profileDialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                    String name = nameInput.getText().toString().trim();
                    if (name.isEmpty()) {
                        nameInput.setError("Digite um nome");
                        nameInput.requestFocus();
                        return;
                    }
                    try {
                        nativeOnProfileCreated(name, selectedFemale[0]);
                        profileDialog.dismiss();
                    } catch (LinkageError | RuntimeException error) {
                        Log.e(TAG, "Não foi possível criar o perfil", error);
                        nameInput.setError("Erro ao criar perfil. Tente novamente.");
                    }
                });
            });
            profileDialog.setOnDismissListener(dialog -> profileDialog = null);
            profileDialog.show();
            Window dialogWindow = profileDialog.getWindow();
            if (dialogWindow != null) {
                dialogWindow.setSoftInputMode(
                        WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE
                                | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
                int dialogWidth = (int) (getResources().getDisplayMetrics().widthPixels * 0.88f);
                dialogWindow.setLayout(dialogWidth, WindowManager.LayoutParams.WRAP_CONTENT);
            }
            nameInput.requestFocus();
        });
    }

    private TextView createGenderOption(String title, String subtitle, int padding) {
        TextView option = new TextView(this);
        option.setText(title + "\n" + subtitle);
        option.setTextSize(15);
        option.setGravity(android.view.Gravity.CENTER);
        option.setPadding(padding / 3, padding / 4, padding / 3, padding / 4);
        option.setMaxLines(2);
        option.setFocusable(true);
        option.setClickable(true);
        return option;
    }

    private void styleGenderOption(TextView option, boolean selected) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(selected ? Color.rgb(35, 145, 75) : Color.rgb(240, 244, 241));
        background.setCornerRadius(
                12 * getResources().getDisplayMetrics().density);
        background.setStroke((int) ((selected ? 2 : 1)
                        * getResources().getDisplayMetrics().density),
                selected ? Color.rgb(25, 115, 58) : Color.rgb(190, 201, 194));
        option.setBackground(background);
        option.setTextColor(selected ? Color.WHITE : Color.rgb(45, 55, 49));
        option.setContentDescription(option.getText().toString()
                + (selected ? ", selecionado" : ""));
        option.setSelected(selected);
    }

    private native void nativeOnProfileCreated(String name, boolean female);

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
