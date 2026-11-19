const navItems = [...document.querySelectorAll('.nav-item')];
const views = [...document.querySelectorAll('.view')];
const sidebar = document.getElementById('sidebar');
const backdrop = document.getElementById('sidebarBackdrop');
const toast = document.getElementById('toast');
let toastTimer;

function showToast(message) {
  toast.textContent = message;
  toast.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove('show'), 2600);
}

function openView(name) {
  navItems.forEach(item => item.classList.toggle('active', item.dataset.view === name));
  views.forEach(view => view.classList.toggle('active', view.id === `view-${name}`));
  sidebar.classList.remove('open');
  backdrop.classList.remove('open');
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

navItems.forEach(item => item.addEventListener('click', () => openView(item.dataset.view)));
document.querySelectorAll('[data-view-action]').forEach(item => {
  item.addEventListener('click', () => openView(item.dataset.viewAction));
});

document.getElementById('menuButton').addEventListener('click', () => {
  sidebar.classList.toggle('open');
  backdrop.classList.toggle('open');
});
backdrop.addEventListener('click', () => {
  sidebar.classList.remove('open');
  backdrop.classList.remove('open');
});

function setTheme(light) {
  document.body.classList.toggle('light', light);
  document.getElementById('themeToggle').classList.toggle('active', !light);
  localStorage.setItem('xenon-theme', light ? 'light' : 'dark');
}

const savedTheme = localStorage.getItem('xenon-theme');
setTheme(savedTheme === 'light');
document.getElementById('themeButton').addEventListener('click', () => setTheme(!document.body.classList.contains('light')));
document.getElementById('themeToggle').addEventListener('click', (event) => {
  setTheme(!document.body.classList.contains('light'));
});

document.querySelectorAll('.settings-row .toggle').forEach(toggle => {
  toggle.addEventListener('click', () => toggle.classList.toggle('active'));
});

const gameInput = document.getElementById('gameInput');
const gameGrid = document.getElementById('gameGrid');
const emptyLibrary = document.getElementById('emptyLibrary');

function chooseGames() {
  gameInput.click();
}

document.getElementById('openGameButton').addEventListener('click', chooseGames);
document.getElementById('addGameButton').addEventListener('click', chooseGames);
document.getElementById('emptyAddButton').addEventListener('click', chooseGames);

gameInput.addEventListener('change', () => {
  const files = [...gameInput.files];
  if (!files.length) return;
  emptyLibrary.classList.add('hidden');
  files.forEach(file => {
    const card = document.createElement('article');
    card.className = 'game-card';
    const title = file.name.replace(/\.[^.]+$/, '') || 'Título sem nome';
    card.innerHTML = `
      <div class="game-cover">X</div>
      <div class="game-info">
        <strong title="${title}">${title}</strong>
        <small>${(file.size / 1024 / 1024).toFixed(1)} MB · Não iniciado</small>
      </div>`;
    card.addEventListener('click', () => showToast('O carregamento de executáveis ainda está em desenvolvimento.'));
    gameGrid.appendChild(card);
  });
  showToast(`${files.length} arquivo(s) preparado(s) na biblioteca.`);
  gameInput.value = '';
});

const searchInput = document.getElementById('searchInput');
searchInput.addEventListener('input', () => {
  const term = searchInput.value.toLowerCase().trim();
  [...gameGrid.querySelectorAll('.game-card')].forEach(card => {
    card.hidden = term && !card.textContent.toLowerCase().includes(term);
  });
});

window.addEventListener('keydown', (event) => {
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k') {
    event.preventDefault();
    searchInput.focus();
  }
  if (event.key === 'Escape') {
    searchInput.blur();
  }
});
