package com.xboxemulator;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.text.InputFilter;
import android.text.InputType;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.TextView;

/**
 * NativeActivity em tela cheia imersiva: esconde a barra de status e os
 * botões de navegação do Android (voltar, início e abas). Eles só aparecem
 * temporariamente ao deslizar a partir da borda da tela.
 */
public class XenonActivity extends NativeActivity {
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
            content.setPadding(padding, padding, padding, 0);

            TextView nameLabel = new TextView(this);
            nameLabel.setText("Nome da conta");
            content.addView(nameLabel);

            EditText nameInput = new EditText(this);
            nameInput.setSingleLine(true);
            nameInput.setHint("Digite o nome");
            nameInput.setInputType(InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_FLAG_CAP_WORDS
                    | InputType.TYPE_TEXT_VARIATION_PERSON_NAME);
            nameInput.setFilters(new InputFilter[] {new InputFilter.LengthFilter(32)});
            content.addView(nameInput);

            TextView genderLabel = new TextView(this);
            genderLabel.setText("Gênero");
            LinearLayout.LayoutParams genderParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            genderParams.topMargin = padding / 2;
            content.addView(genderLabel, genderParams);

            RadioGroup genderOptions = new RadioGroup(this);
            genderOptions.setOrientation(RadioGroup.HORIZONTAL);
            RadioButton maleOption = new RadioButton(this);
            maleOption.setText("Masculino");
            maleOption.setId(View.generateViewId());
            RadioButton femaleOption = new RadioButton(this);
            femaleOption.setText("Feminino");
            femaleOption.setId(View.generateViewId());
            genderOptions.addView(maleOption);
            genderOptions.addView(femaleOption);
            genderOptions.check(maleOption.getId());
            content.addView(genderOptions);

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
                    nativeOnProfileCreated(name, genderOptions.getCheckedRadioButtonId()
                            == femaleOption.getId());
                    profileDialog.dismiss();
                });
            });
            profileDialog.setOnDismissListener(dialog -> profileDialog = null);
            profileDialog.show();
            Window dialogWindow = profileDialog.getWindow();
            if (dialogWindow != null) {
                dialogWindow.setSoftInputMode(
                        WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            }
            nameInput.requestFocus();
        });
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
