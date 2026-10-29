/*
 * FoodRush – Interactive Frontend Controller
 * Connects to C REST Backend with Threadpool Telemetry
 */

const API_BASE = window.location.origin;

// Application State
const state = {
  menu: [],
  categories: ['ALL', 'Pizza', 'Burgers', 'Indian', 'Salads', 'Desserts', 'Drinks'],
  activeCategory: 'ALL',
  searchQuery: '',
  sortBy: 'popular',
  cart: JSON.parse(localStorage.getItem('foodrush_cart') || '[]'),
  token: localStorage.getItem('foodrush_token') || '',
  user: JSON.parse(localStorage.getItem('foodrush_user') || 'null'),
  analytics: null,
  orders: [],
  dismissedOrderIds: JSON.parse(localStorage.getItem('foodrush_dismissed_orders') || '[]'),
  trackedOrderId: null,
  appliedPromo: null,
  audioEnabled: localStorage.getItem('foodrush_audio') !== 'false',
  logsPollInterval: null,
  analyticsPollInterval: null
};

// ── Innovation: Web Audio Synthesizer (Zero-dependency Audio Chimes) ──────────
const AudioContextClass = window.AudioContext || window.webkitAudioContext;
let audioCtx = null;

function playChime(type = 'add') {
  if (!state.audioEnabled) return;
  try {
    if (!audioCtx && AudioContextClass) {
      audioCtx = new AudioContextClass();
    }
    if (audioCtx && audioCtx.state === 'suspended') {
      audioCtx.resume();
    }
    if (!audioCtx) return;

    const now = audioCtx.currentTime;
    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.connect(gain);
    gain.connect(audioCtx.destination);

    if (type === 'add') {
      osc.type = 'sine';
      osc.frequency.setValueAtTime(523.25, now); // C5
      osc.frequency.exponentialRampToValueAtTime(783.99, now + 0.12); // G5
      gain.gain.setValueAtTime(0.12, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.18);
      osc.start(now);
      osc.stop(now + 0.18);
    } else if (type === 'success') {
      osc.type = 'triangle';
      osc.frequency.setValueAtTime(440, now); // A4
      osc.frequency.exponentialRampToValueAtTime(659.25, now + 0.1); // E5
      osc.frequency.exponentialRampToValueAtTime(880, now + 0.22); // A5
      gain.gain.setValueAtTime(0.18, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.35);
      osc.start(now);
      osc.stop(now + 0.35);
    }
  } catch (err) {
    // Audio optional fallback
  }
}

// ── Initialization ────────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  initEventListeners();
  loadMenu();
  loadAnalytics();
  loadOrders();
  renderCart();
  updateAuthUI();
  updateAudioUI();

  // Periodic polling for real-time OS Threadpool Concurrency updates
  state.analyticsPollInterval = setInterval(loadAnalytics, 2500);
  setInterval(loadOrders, 4000);
});

// ── Event Listeners ───────────────────────────────────────────────────────────
function initEventListeners() {
  // Category tabs
  document.querySelectorAll('.cat-tab').forEach(tab => {
    tab.addEventListener('click', (e) => {
      document.querySelectorAll('.cat-tab').forEach(t => t.classList.remove('active'));
      e.target.classList.add('active');
      state.activeCategory = e.target.dataset.category;
      renderMenu();
    });
  });

  // Search input
  const searchInput = document.getElementById('searchInput');
  const clearSearchBtn = document.getElementById('clearSearchBtn');
  searchInput.addEventListener('input', (e) => {
    state.searchQuery = e.target.value.trim().toLowerCase();
    clearSearchBtn.style.display = state.searchQuery ? 'block' : 'none';
    renderMenu();
  });
  clearSearchBtn.addEventListener('click', () => {
    searchInput.value = '';
    state.searchQuery = '';
    clearSearchBtn.style.display = 'none';
    renderMenu();
  });

  // Sort dropdown
  document.getElementById('sortSelect').addEventListener('change', (e) => {
    state.sortBy = e.target.value;
    renderMenu();
  });

  // Cart Drawer toggles
  const cartDrawer = document.getElementById('cartDrawer');
  const cartBackdrop = document.getElementById('cartBackdrop');
  const openCart = () => { cartDrawer.classList.add('open'); cartBackdrop.classList.add('open'); };
  const closeCart = () => { cartDrawer.classList.remove('open'); cartBackdrop.classList.remove('open'); };

  document.getElementById('cartToggleBtn').addEventListener('click', openCart);
  document.getElementById('closeCartBtn').addEventListener('click', closeCart);
  cartBackdrop.addEventListener('click', closeCart);

  // Audio Toggle
  document.getElementById('audioToggleBtn').addEventListener('click', toggleAudio);

  // Promo Code controls
  document.getElementById('applyPromoBtn').addEventListener('click', applyPromoCode);
  document.getElementById('removePromoBtn').addEventListener('click', removePromoCode);

  // Order Tracker Modal close
  document.getElementById('closeTrackerBtn').addEventListener('click', () => {
    document.getElementById('trackerModal').classList.remove('open');
  });

  // Monitor Modal
  const monitorModal = document.getElementById('monitorModal');
  const openMonitor = () => {
    monitorModal.classList.add('open');
    startLogsStream();
  };
  const closeMonitor = () => {
    monitorModal.classList.remove('open');
    stopLogsStream();
  };
  document.getElementById('openMonitorBtn').addEventListener('click', openMonitor);
  document.getElementById('closeMonitorBtn').addEventListener('click', closeMonitor);

  // Admin Modal
  const adminModal = document.getElementById('adminModal');
  document.getElementById('adminPortalBtn').addEventListener('click', () => {
    adminModal.classList.add('open');
    populateStockTable();
  });
  document.getElementById('closeAdminBtn').addEventListener('click', () => {
    adminModal.classList.remove('open');
  });

  // Admin Tabs
  document.querySelectorAll('.adm-tab').forEach(tab => {
    tab.addEventListener('click', (e) => {
      document.querySelectorAll('.adm-tab').forEach(t => t.classList.remove('active'));
      document.querySelectorAll('.adm-tab-panel').forEach(p => p.classList.remove('active'));
      e.target.classList.add('active');
      document.getElementById(e.target.dataset.tab).classList.add('active');
    });
  });

  // Checkout Modal
  const checkoutModal = document.getElementById('checkoutModal');
  document.getElementById('checkoutBtn').addEventListener('click', () => {
    if (state.cart.length === 0) return;
    closeCart();
    // Pre-fill user profile if logged in
    if (state.user) {
      const u = state.user;
      const nameVal = u.full_name || u.name || u.customer_name || u.username || '';
      const phoneVal = u.phone || u.customer_phone || '';
      const emailVal = u.email || u.customer_email || '';
      const addrVal = u.address || u.delivery_address || '';
      if (nameVal) document.getElementById('custName').value = nameVal;
      if (phoneVal) document.getElementById('custPhone').value = phoneVal;
      if (emailVal) document.getElementById('custEmail').value = emailVal;
      if (addrVal) document.getElementById('custAddress').value = addrVal;
    }
    const finalTotal = calculateTotals().total;
    document.getElementById('checkoutFinalTotal').textContent = `₹${finalTotal.toFixed(2)}`;
    checkoutModal.classList.add('open');
  });
  document.getElementById('closeCheckoutBtn').addEventListener('click', () => {
    checkoutModal.classList.remove('open');
  });

  // Checkout Form Submission
  document.getElementById('checkoutForm').addEventListener('submit', handleCheckoutSubmit);

  // Auth Modal
  const authModal = document.getElementById('authModal');
  document.getElementById('authBtn').addEventListener('click', () => {
    if (state.token) {
      // Logout
      logoutUser();
    } else {
      authModal.classList.add('open');
    }
  });
  document.getElementById('closeAuthBtn').addEventListener('click', () => {
    authModal.classList.remove('open');
  });

  // Auth Tabs (Login / Register)
  document.getElementById('tabLogin').addEventListener('click', () => {
    document.getElementById('tabLogin').classList.add('active');
    document.getElementById('tabRegister').classList.remove('active');
    document.getElementById('loginForm').classList.add('active');
    document.getElementById('registerForm').classList.remove('active');
    document.getElementById('authModalTitle').textContent = 'Sign In to FoodRush';
  });
  document.getElementById('tabRegister').addEventListener('click', () => {
    document.getElementById('tabRegister').classList.add('active');
    document.getElementById('tabLogin').classList.remove('active');
    document.getElementById('registerForm').classList.add('active');
    document.getElementById('loginForm').classList.remove('active');
    document.getElementById('authModalTitle').textContent = 'Create FoodRush Account';
  });

  // Auth Forms
  document.getElementById('loginForm').addEventListener('submit', handleLogin);
  document.getElementById('registerForm').addEventListener('submit', handleRegister);

  // Quick Credential buttons
  document.getElementById('fillAdminCreds').addEventListener('click', () => {
    document.getElementById('tabLogin').click();
    document.getElementById('loginUser').value = 'admin';
    document.getElementById('loginPass').value = 'admin123';
  });
  document.getElementById('fillCustomerCreds').addEventListener('click', () => {
    document.getElementById('tabLogin').click();
    document.getElementById('loginUser').value = 'john';
    document.getElementById('loginPass').value = 'password123';
  });
  document.getElementById('quickLoginAdmin').addEventListener('click', () => {
    quickLogin('admin', 'admin123');
  });
  document.getElementById('quickLoginCustomer').addEventListener('click', () => {
    quickLogin('john', 'password123');
  });

  // Simulation controls
  document.getElementById('runSimBtn').addEventListener('click', () => {
    const count = parseInt(document.getElementById('simCountInput').value) || 20;
    triggerSimulation(count);
  });
  document.getElementById('sim10Btn').addEventListener('click', () => triggerSimulation(10));
  document.getElementById('sim50Btn').addEventListener('click', () => triggerSimulation(50));
  document.getElementById('heroSimulateBtn').addEventListener('click', () => {
    triggerSimulation(25);
    openMonitor();
  });

  // Refresh orders button
  document.getElementById('refreshOrdersBtn').addEventListener('click', () => {
    loadOrders();
    showToast('Orders refreshed from C Backend', 'info');
  });
}

// ── API: Load Menu ─────────────────────────────────────────────────────────────
async function loadMenu() {
  try {
    const res = await fetch(`${API_BASE}/api/menu`);
    const data = await res.json();
    if (data.status === 'ok') {
      state.menu = data.data;
      renderMenu();
      populateStockTable();
    }
  } catch (err) {
    console.error('Failed to load menu:', err);
    document.getElementById('menuGrid').innerHTML = `
      <div class="loading-state">
        <p style="color:var(--danger)">Failed to connect to FoodRush Backend on port 8080.</p>
      </div>`;
  }
}

// ── Render Menu Cards ─────────────────────────────────────────────────────────
function renderMenu() {
  const grid = document.getElementById('menuGrid');
  let filtered = state.menu.filter(item => {
    const matchCat = (state.activeCategory === 'ALL' || item.category === state.activeCategory);
    const matchSearch = (!state.searchQuery ||
      item.name.toLowerCase().includes(state.searchQuery) ||
      item.description.toLowerCase().includes(state.searchQuery) ||
      item.category.toLowerCase().includes(state.searchQuery));
    return matchCat && matchSearch;
  });

  // Sorting
  if (state.sortBy === 'price-asc') filtered.sort((a, b) => a.price - b.price);
  else if (state.sortBy === 'price-desc') filtered.sort((a, b) => b.price - a.price);
  else if (state.sortBy === 'rating') filtered.sort((a, b) => b.rating - a.rating);
  else filtered.sort((a, b) => b.rating_count - a.rating_count);

  document.getElementById('itemCountDisplay').textContent = `Showing ${filtered.length} delicious dishes`;

  if (filtered.length === 0) {
    grid.innerHTML = `
      <div class="loading-state">
        <p>No dishes found matching your criteria. Try another category or search term!</p>
      </div>`;
    return;
  }

  grid.innerHTML = filtered.map(item => {
    const outOfStock = item.stock <= 0 || !item.is_available;
    return `
      <article class="menu-card" data-id="${item.id}">
        <div class="card-image-wrap">
          <img src="${getFoodImageUrl(item)}" data-fallback="${getFoodImageFallback(item)}" alt="${item.name}" loading="lazy" onerror="this.onerror=null;this.src=this.dataset.fallback">
          <span class="badge-category">${item.category}</span>
          <span class="card-rating">★ ${item.rating.toFixed(1)} <small>(${item.rating_count})</small></span>
        </div>
        <div class="card-body">
          <h3 class="card-title">${item.name}</h3>
          <p class="card-desc">${item.description}</p>
          <div class="card-footer">
            <div>
              <span class="card-price">₹${item.price.toFixed(2)}</span>
              <span class="card-stock ${item.stock < 10 ? 'text-warning' : ''}">
                ${outOfStock ? '🔴 Out of Stock' : `🟢 Stock: ${item.stock} left`}
              </span>
            </div>
            <button class="btn-add-cart" onclick="addToCart(${item.id})" ${outOfStock ? 'disabled' : ''}>
              ${outOfStock ? 'Unavailable' : '+ Add to Cart'}
            </button>
          </div>
        </div>
      </article>
    `;
  }).join('');
}

function getFoodImageUrl(item) {
  const menuItem = state.menu.find(entry => entry.id === item.id);
  const category = item.category || (menuItem && menuItem.category);
  const imageName = String(item.name || (menuItem && menuItem.name) || '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-|-$/g, '');
  return imageName
    ? `images/food/food-${imageName}.svg`
    : getFoodImageFallback({ category });
}

function getFoodImageFallback(item) {
  const menuItem = state.menu.find(entry => entry.id === item.id);
  const category = item.category || (menuItem && menuItem.category);
  const categoryFile = String(category || 'Salads').toLowerCase().replace(/[^a-z]/g, '');
  return `images/food/${categoryFile || 'salads'}-default.svg`;
}

// ── Cart Operations ───────────────────────────────────────────────────────────
window.addToCart = function(itemId) {
  const item = state.menu.find(m => m.id === itemId);
  if (!item || item.stock <= 0) return;

  const existing = state.cart.find(c => c.id === itemId);
  if (existing) {
    if (existing.qty >= item.stock) {
      showToast(`Only ${item.stock} in stock!`, 'error');
      return;
    }
    existing.qty += 1;
  } else {
    state.cart.push({
      id: item.id,
      name: item.name,
      price: item.price,
      image_url: item.image_url,
      category: item.category,
      qty: 1
    });
  }

  saveCart();
  renderCart();
  playChime('add');
  showToast(`Added ${item.name} to cart!`, 'success');
};

function saveCart() {
  localStorage.setItem('foodrush_cart', JSON.stringify(state.cart));
}

function calculateTotals() {
  const subtotal = state.cart.reduce((sum, item) => sum + (item.price * item.qty), 0);
  const tax = subtotal * 0.05; // 5% GST
  let delivery = subtotal > 0 ? (subtotal > 500 ? 0 : 30) : 0;
  let discount = 0;

  if (state.appliedPromo && subtotal > 0) {
    const code = state.appliedPromo.toUpperCase();
    if (code === 'RUSH50') {
      discount = subtotal >= 200 ? 50 : 0;
    } else if (code === 'FREEDEL') {
      discount = delivery;
      delivery = 0;
    } else if (code === 'SUPER20') {
      discount = Math.min(100, Math.round(subtotal * 0.20));
    } else if (code === 'CHEF10') {
      discount = Math.round(subtotal * 0.10);
    }
  }

  const total = Math.max(0, subtotal + tax + delivery - discount);
  return { subtotal, tax, delivery, discount, total };
}

function renderCart() {
  const totalItems = state.cart.reduce((sum, i) => sum + i.qty, 0);
  document.getElementById('cartCount').textContent = totalItems;
  document.getElementById('cartDrawerCount').textContent = totalItems;

  const list = document.getElementById('cartItemsList');
  const checkoutBtn = document.getElementById('checkoutBtn');

  if (state.cart.length === 0) {
    list.innerHTML = `
      <div class="empty-cart-view">
        <span class="empty-icon">🛒</span>
        <h4>Your cart is empty</h4>
        <p>Explore the menu and add your favorite dishes!</p>
      </div>`;
    checkoutBtn.disabled = true;
  } else {
    list.innerHTML = state.cart.map(item => `
      <div class="cart-item-row">
        <img src="${getFoodImageUrl(item)}" data-fallback="${getFoodImageFallback(item)}" alt="${item.name}" class="cart-item-thumb" onerror="this.onerror=null;this.src=this.dataset.fallback">
        <div class="cart-item-info">
          <div class="cart-item-title">${item.name}</div>
          <div class="cart-item-price">₹${item.price.toFixed(2)} each</div>
          <div class="cart-qty-ctrl">
            <button class="btn-qty" onclick="updateCartQty(${item.id}, -1)">−</button>
            <span style="font-weight:700; font-family:'JetBrains Mono'">${item.qty}</span>
            <button class="btn-qty" onclick="updateCartQty(${item.id}, 1)">+</button>
          </div>
        </div>
        <div style="text-align:right">
          <strong style="color:var(--accent-secondary); font-family:'JetBrains Mono'">₹${(item.price * item.qty).toFixed(2)}</strong>
          <div style="margin-top:6px">
            <button class="btn-small ghost-btn" onclick="removeFromCart(${item.id})">Remove</button>
          </div>
        </div>
      </div>
    `).join('');
    checkoutBtn.disabled = false;
  }

  const { subtotal, tax, delivery, discount, total } = calculateTotals();
  document.getElementById('cartSubtotal').textContent = `₹${subtotal.toFixed(2)}`;
  document.getElementById('cartTax').textContent = `₹${tax.toFixed(2)}`;
  document.getElementById('cartDelivery').textContent = delivery === 0 ? 'FREE' : `₹${delivery.toFixed(2)}`;
  
  const promoRow = document.getElementById('promoRow');
  if (discount > 0) {
    promoRow.style.display = 'flex';
    document.getElementById('cartDiscount').textContent = `-₹${discount.toFixed(2)}`;
  } else {
    promoRow.style.display = 'none';
  }

  document.getElementById('cartTotal').textContent = `₹${total.toFixed(2)}`;
}

window.updateCartQty = function(id, delta) {
  const item = state.cart.find(c => c.id === id);
  if (!item) return;
  item.qty += delta;
  if (item.qty <= 0) {
    state.cart = state.cart.filter(c => c.id !== id);
  }
  saveCart();
  renderCart();
};

window.removeFromCart = function(id) {
  state.cart = state.cart.filter(c => c.id !== id);
  saveCart();
  renderCart();
};

// ── API: Submit Order (Multithreaded C Processing) ─────────────────────────────
async function handleCheckoutSubmit(e) {
  e.preventDefault();
  if (state.cart.length === 0) {
    showToast('Your cart is empty', 'error');
    return;
  }

  const customerName = (document.getElementById('custName')?.value || '').trim();
  const customerPhone = (document.getElementById('custPhone')?.value || '').trim();
  const customerEmail = (document.getElementById('custEmail')?.value || '').trim();
  const deliveryAddress = (document.getElementById('custAddress')?.value || '').trim();

  // Validate required fields
  if (!customerName || !customerEmail || !deliveryAddress) {
    showToast('customer_name, customer_email and delivery_address are required', 'error');
    return;
  }
  if (!customerPhone) {
    showToast('Phone number is required', 'error');
    return;
  }

  const btn = document.getElementById('submitOrderBtn');
  btn.disabled = true;
  btn.textContent = 'Enqueuing into OS Thread Pool...';

  const orderPayload = {
    customer_name: customerName,
    name: customerName,
    full_name: customerName,
    customerName: customerName,
    custName: customerName,
    customer_email: customerEmail,
    email: customerEmail,
    customerEmail: customerEmail,
    custEmail: customerEmail,
    delivery_address: deliveryAddress,
    address: deliveryAddress,
    deliveryAddress: deliveryAddress,
    custAddress: deliveryAddress,
    phone: customerPhone,
    customer_phone: customerPhone,
    customerPhone: customerPhone,
    custPhone: customerPhone,
    items: state.cart.map(c => ({
      menu_item_id: c.id,
      item_id: c.id,
      quantity: c.qty,
      notes: (document.getElementById('custNotes')?.value || '').trim()
    }))
  };

  try {
    const headers = { 'Content-Type': 'application/json' };
    if (state.token) headers['Authorization'] = `Bearer ${state.token}`;

    const res = await fetch(`${API_BASE}/api/orders`, {
      method: 'POST',
      headers,
      body: JSON.stringify(orderPayload)
    });
    const data = await res.json();

    if (res.ok && data.status === 'ok') {
      playChime('success');
      showToast(`Order #${data.order_id} placed! Dispatched to C Worker Thread.`, 'success');
      state.cart = [];
      state.appliedPromo = null;
      const pBadge = document.getElementById('promoBadge');
      if (pBadge) pBadge.style.display = 'none';
      const pInput = document.getElementById('promoInput');
      if (pInput) pInput.value = '';

      saveCart();
      renderCart();
      document.getElementById('checkoutModal').classList.remove('open');
      loadOrders();
      loadAnalytics();
      loadMenu(); // Refresh stock counts

      // Automatically launch the interactive Live Order Tracker Modal!
      openOrderTrackerModal({
        id: data.order_id,
        customer_name: customerName,
        customer_phone: customerPhone,
        delivery_address: deliveryAddress,
        total_amount: data.total_amount,
        status: 'CONFIRMED',
        status_str: 'Confirmed',
        processed_by_tid: 4
      });
    } else {
      showToast(data.message || 'Failed to submit order', 'error');
    }
  } catch (err) {
    console.error('Order error:', err);
    showToast('Network error while placing order', 'error');
  } finally {
    btn.disabled = false;
    btn.textContent = 'Confirm Order & Dispatch Thread';
  }
}

// ── Innovation: Promo Code Engine ─────────────────────────────────────────────
function applyPromoCode() {
  const input = document.getElementById('promoInput');
  const code = input.value.trim().toUpperCase();
  if (!code) {
    showToast('Please enter a coupon code', 'error');
    return;
  }
  const { subtotal } = calculateTotals();
  if (subtotal <= 0) {
    showToast('Add items to cart before applying coupons', 'error');
    return;
  }

  if (code === 'RUSH50') {
    if (subtotal < 200) {
      showToast('RUSH50 requires minimum cart value of ₹200', 'error');
      return;
    }
    state.appliedPromo = code;
    showToast('Coupon RUSH50 applied! ₹50 OFF saved.', 'success');
  } else if (code === 'FREEDEL') {
    state.appliedPromo = code;
    showToast('Coupon FREEDEL applied! Free Delivery activated.', 'success');
  } else if (code === 'SUPER20') {
    state.appliedPromo = code;
    showToast('Coupon SUPER20 applied! 20% Discount unlocked.', 'success');
  } else if (code === 'CHEF10') {
    state.appliedPromo = code;
    showToast('Coupon CHEF10 applied! 10% Discount applied.', 'success');
  } else {
    showToast('Invalid coupon code. Try RUSH50 or FREEDEL', 'error');
    return;
  }

  playChime('add');
  const badge = document.getElementById('promoBadge');
  const badgeText = document.getElementById('promoBadgeText');
  badge.style.display = 'flex';
  badgeText.textContent = `✓ Coupon ${code} Active`;
  renderCart();
}

function removePromoCode() {
  state.appliedPromo = null;
  document.getElementById('promoBadge').style.display = 'none';
  document.getElementById('promoInput').value = '';
  renderCart();
  showToast('Coupon removed', 'info');
}

// ── Innovation: Audio Control ─────────────────────────────────────────────────
function toggleAudio() {
  state.audioEnabled = !state.audioEnabled;
  localStorage.setItem('foodrush_audio', state.audioEnabled ? 'true' : 'false');
  updateAudioUI();
  if (state.audioEnabled) playChime('add');
  showToast(`Sound effects ${state.audioEnabled ? 'Enabled 🔊' : 'Muted 🔇'}`, 'info');
}

function updateAudioUI() {
  const icon = document.getElementById('audioIcon');
  const text = document.getElementById('audioBtnText');
  if (icon && text) {
    icon.textContent = state.audioEnabled ? '🔊' : '🔇';
    text.textContent = state.audioEnabled ? 'Sound: ON' : 'Sound: OFF';
  }
}

// ── Innovation: Interactive Live Order Tracker Modal ──────────────────────────
function openOrderTrackerModal(ord) {
  if (!ord) return;
  state.trackedOrderId = ord.id;
  document.getElementById('trackModalOrderId').textContent = `Tracking Order #${ord.id}`;
  document.getElementById('trackDestLabel').textContent = ord.delivery_address ? ord.delivery_address.slice(0, 20) + '...' : 'Destination';
  document.getElementById('trackAddressDisplay').textContent = ord.delivery_address || 'Customer Location';
  document.getElementById('trackTotalDisplay').textContent = `₹${(ord.total_amount || 0).toFixed(2)}`;
  document.getElementById('trackTidDisplay').textContent = `Worker TID: ${ord.processed_by_tid || 4}`;

  // Stepper state
  const sStr = String(ord.status_str || ord.status || '').toUpperCase();
  const nodes = [1, 2, 3, 4, 5].map(i => document.getElementById(`step${i}`));
  const lines = [1, 2, 3, 4].map(i => document.getElementById(`line${i}`));
  const rider = document.getElementById('riderMarker');

  nodes.forEach(n => { if (n) n.className = 'step-node'; });
  lines.forEach(l => { if (l) l.className = 'step-line'; });

  let activeIdx = 1; // 1-5
  let riderLeft = '25%';

  if (sStr.includes('DELIVERED') || sStr === '4') {
    activeIdx = 5;
    riderLeft = '88%';
    document.getElementById('trackEtaDisplay').textContent = '✅ Order Delivered!';
  } else if (sStr.includes('OUT') || sStr === '3') {
    activeIdx = 4;
    riderLeft = '70%';
    document.getElementById('trackEtaDisplay').textContent = '🛵 Rider On The Way (~8 Mins)';
  } else if (sStr.includes('PREPARING') || sStr === '2') {
    activeIdx = 3;
    riderLeft = '45%';
    document.getElementById('trackEtaDisplay').textContent = '🍳 In Kitchen Oven (~16 Mins)';
  } else if (sStr.includes('CONFIRMED') || sStr === '1') {
    activeIdx = 2;
    riderLeft = '28%';
    document.getElementById('trackEtaDisplay').textContent = '⚡ Dispatched to Kitchen Thread (~20 Mins)';
  } else {
    activeIdx = 1;
    riderLeft = '15%';
    document.getElementById('trackEtaDisplay').textContent = '📝 Order Enqueued in Circular Buffer';
  }

  for (let i = 0; i < activeIdx; i++) {
    if (nodes[i]) nodes[i].classList.add(i === activeIdx - 1 ? 'active' : 'completed');
    if (i < activeIdx - 1 && lines[i]) lines[i].classList.add('active');
  }

  if (rider) {
    rider.style.left = riderLeft;
  }

  document.getElementById('trackerModal').classList.add('open');
}

// ── API: Load Orders ──────────────────────────────────────────────────────────
async function loadOrders() {
  try {
    const headers = {};
    if (state.token) headers['Authorization'] = `Bearer ${state.token}`;
    const res = await fetch(`${API_BASE}/api/orders`, { headers });
    const data = await res.json();
    if (data.status === 'ok') {
      state.orders = data.data;
      renderOrdersTable();
      if (state.trackedOrderId !== null && document.getElementById('trackerModal').classList.contains('open')) {
        const trackedOrder = state.orders.find(order => String(order.id) === String(state.trackedOrderId));
        if (trackedOrder) openOrderTrackerModal(trackedOrder);
      }
    }
  } catch (err) {
    console.error('Error loading orders:', err);
  }
}

function renderOrdersTable() {
  const tbody = document.getElementById('ordersTableBody');
  const visibleOrders = (state.orders || []).filter(order =>
    !state.dismissedOrderIds.includes(String(order.id))
  );
  if (visibleOrders.length === 0) {
    tbody.innerHTML = `<tr><td colspan="7" class="empty-state">No orders yet. Place an order above to watch the C threads work!</td></tr>`;
    return;
  }

  tbody.innerHTML = visibleOrders.slice(0, 15).map(ord => {
    let statusClass = 'status-pending';
    if (ord.status === 'CONFIRMED' || ord.status === 1) statusClass = 'status-confirmed';
    else if (ord.status === 'PREPARING' || ord.status === 2) statusClass = 'status-preparing';
    else if (ord.status === 'OUT_FOR_DELIVERY' || ord.status === 3) statusClass = 'status-out';
    else if (ord.status === 'DELIVERED' || ord.status === 4) statusClass = 'status-delivered';
    else if (ord.status === 'CANCELLED') statusClass = 'status-cancelled';
    const isDelivered = Number(ord.status) === 4 ||
      String(ord.status_str || ord.status).toUpperCase() === 'DELIVERED';

    return `
      <tr>
        <td><strong>${ord.id}</strong></td>
        <td>
          <div>${ord.customer_name}</div>
          <small style="color:var(--text-dim)">${ord.customer_phone || ord.phone || ''}</small>
        </td>
        <td>
          <span style="font-weight:700; color:var(--accent-secondary); font-family:'JetBrains Mono'">₹${ord.total_amount.toFixed(2)}</span>
          <div><small style="color:var(--text-muted)">${ord.item_count || 1} items</small></div>
        </td>
        <td><span class="status-badge ${statusClass}">${ord.status_str || ord.status}</span></td>
        <td>
          <span class="thread-badge">
            <span class="pulse-dot"></span> TID: ${ord.worker_thread_id > 0 ? ord.worker_thread_id : (ord.processed_by_tid > 0 ? ord.processed_by_tid : 'Pool')}
          </span>
        </td>
        <td><small style="color:var(--text-dim)">${ord.created_at || 'Just now'}</small></td>
        <td>
          ${isDelivered
            ? `<button type="button" class="btn-small ghost-btn" onclick="dismissDeliveredOrder('${ord.id}')">Remove</button>`
            : `<button type="button" class="btn-small ghost-btn" aria-label="Track order ${ord.id}" onclick="viewOrderDetails('${ord.id}')">Track</button>`}
        </td>
      </tr>
    `;
  }).join('');
}

window.dismissDeliveredOrder = function(orderId) {
  const order = state.orders.find(item => String(item.id) === String(orderId));
  if (!order || !(Number(order.status) === 4 ||
      String(order.status_str || order.status).toUpperCase() === 'DELIVERED')) return;

  const id = String(order.id);
  if (!state.dismissedOrderIds.includes(id)) {
    state.dismissedOrderIds.push(id);
    localStorage.setItem('foodrush_dismissed_orders', JSON.stringify(state.dismissedOrderIds));
  }
  renderOrdersTable();
  showToast(`Delivered order #${id} removed from this list`, 'success');
};

window.viewOrderDetails = function(orderId) {
  const ord = state.orders.find(order =>
    String(order.id) === String(orderId) || String(order.order_id) === String(orderId)
  );
  if (!ord) {
    showToast('Order not found. Refresh the orders list and try again.', 'error');
    return;
  }
  openOrderTrackerModal(ord);
};

// ── API: Load Analytics & Update OS Concurrency Tickers ───────────────────────
async function loadAnalytics() {
  try {
    const res = await fetch(`${API_BASE}/api/analytics`);
    const data = await res.json();
    if (data.status === 'ok') {
      const d = data.data;
      state.analytics = d;

      // Update Top Ticker
      document.getElementById('tickerWorkers').textContent = `${d.active_workers} / 8 Active`;
      document.getElementById('tickerQueue').textContent = `${d.queued_tasks} / 128`;
      document.getElementById('tickerCompleted').textContent = d.completed_tasks;

      // Update Hero Stats Card
      document.getElementById('heroStatWorkers').textContent = d.active_workers;
      document.getElementById('heroStatDelivered').textContent = d.delivered_orders;
      document.getElementById('heroStatRevenue').textContent = `₹${d.total_revenue.toFixed(2)}`;
      document.getElementById('heroStatQueue').textContent = d.queued_tasks;
      document.getElementById('activeWorkerBadge').textContent = d.active_workers;

      // Update Monitor Modal Metrics
      document.getElementById('monActiveWorkers').textContent = `${d.active_workers} / 8`;
      document.getElementById('monQueueDepth').textContent = `${d.queued_tasks} / 128`;
      document.getElementById('monCompleted').textContent = d.completed_tasks;

      renderWorkerChips(d.active_workers, d.queued_tasks);
    }
  } catch (err) {
    console.error('Analytics poll error:', err);
  }
}

function renderWorkerChips(activeCount, queueDepth) {
  const container = document.getElementById('workerChipsContainer');
  let html = '';
  for (let i = 1; i <= 8; i++) {
    const isBusy = (queueDepth > 0 && i <= activeCount);
    html += `
      <div class="worker-chip ${isBusy ? 'busy' : ''}">
        <span class="chip-tid">Thread #${i} (TID: ${i+1})</span>
        <span class="chip-status ${isBusy ? 'text-accent' : 'text-success'}">
          ${isBusy ? '⚡ Processing Task' : '✓ Idle / Ready'}
        </span>
      </div>
    `;
  }
  container.innerHTML = html;
}

// ── Live Kernel Logs Stream ───────────────────────────────────────────────────
function startLogsStream() {
  stopLogsStream();
  pollLogs();
  state.logsPollInterval = setInterval(pollLogs, 1500);
}

function stopLogsStream() {
  if (state.logsPollInterval) {
    clearInterval(state.logsPollInterval);
    state.logsPollInterval = null;
  }
}

async function pollLogs() {
  try {
    const res = await fetch(`${API_BASE}/api/logs`);
    const data = await res.json();
    if (data.status === 'ok') {
      const terminal = document.getElementById('terminalLogs');
      if (Array.isArray(data.data) && data.data.length > 0) {
        terminal.textContent = data.data.map(l => `[${l.created_at || 'LOG'}] ${l.message || JSON.stringify(l)}`).join('\n');
      } else {
        terminal.textContent = `[KERNEL] FoodRush POSIX Threadpool Active (8 threads listening on port 8080)\n[CONCURRENCY] Mutex locks active; Task queue ready.`;
      }
      terminal.scrollTop = terminal.scrollHeight;
    }
  } catch (err) {
    console.error('Log fetch error:', err);
  }
}

// ── Admin: Simulation & Stock Management ──────────────────────────────────────
async function triggerSimulation(count) {
  const statusBox = document.getElementById('simStatusBox');
  statusBox.textContent = `Dispatching ${count} concurrent orders into C Threadpool...`;

  try {
    const headers = { 'Content-Type': 'application/json' };
    if (state.token) headers['Authorization'] = `Bearer ${state.token}`;

    const res = await fetch(`${API_BASE}/api/simulate`, {
      method: 'POST',
      headers,
      body: JSON.stringify({ count })
    });
    const data = await res.json();

    if (res.ok && data.status === 'ok') {
      statusBox.innerHTML = `✅ <strong>Success:</strong> ${data.submitted} orders submitted across thread pool! (${data.rejected} rejected by queue limit)`;
      showToast(`Simulation started: ${data.submitted} orders dispatched!`, 'success');
      loadAnalytics();
      loadOrders();
    } else {
      statusBox.textContent = data.message || 'Simulation requires Admin authentication.';
      showToast(data.message || 'Admin login required to simulate', 'error');
    }
  } catch (err) {
    statusBox.textContent = 'Network error while triggering simulation.';
  }
}

function populateStockTable() {
  const tbody = document.getElementById('stockTableBody');
  if (!tbody || state.menu.length === 0) return;

  tbody.innerHTML = state.menu.map(item => `
    <tr>
      <td><strong>${item.name}</strong></td>
      <td>${item.category}</td>
      <td>₹${item.price.toFixed(2)}</td>
      <td>
        <input type="number" id="stock_${item.id}" value="${item.stock}" style="width:65px" class="custom-input">
      </td>
      <td>
        <button class="btn-small btn-primary" onclick="updateItemStock(${item.id})">Save</button>
      </td>
    </tr>
  `).join('');
}

window.updateItemStock = async function(id) {
  const input = document.getElementById(`stock_${id}`);
  const newStock = parseInt(input.value) || 0;

  try {
    const headers = { 'Content-Type': 'application/json' };
    if (state.token) headers['Authorization'] = `Bearer ${state.token}`;

    const res = await fetch(`${API_BASE}/api/menu/${id}`, {
      method: 'PUT',
      headers,
      body: JSON.stringify({ stock: newStock, is_available: newStock > 0 })
    });
    const data = await res.json();

    if (res.ok && data.status === 'ok') {
      showToast(`Stock updated for item #${id}`, 'success');
      loadMenu();
    } else {
      showToast(data.message || 'Admin authentication required', 'error');
    }
  } catch (err) {
    showToast('Failed to update stock', 'error');
  }
};

// ── Auth Handling ─────────────────────────────────────────────────────────────
async function handleLogin(e) {
  e.preventDefault();
  const username = document.getElementById('loginUser').value.trim();
  const password = document.getElementById('loginPass').value;

  try {
    const res = await fetch(`${API_BASE}/api/auth/login`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password })
    });
    const data = await res.json();

    if (res.ok && data.status === 'ok') {
      setAuthSession(data.token, data.user);
      document.getElementById('authModal').classList.remove('open');
      showToast(`Welcome back, ${data.user.full_name || data.user.username}!`, 'success');
    } else {
      showToast(data.message || 'Invalid username or password', 'error');
    }
  } catch (err) {
    showToast('Login connection failed', 'error');
  }
}

async function handleRegister(e) {
  e.preventDefault();
  const payload = {
    username: document.getElementById('regUser').value.trim(),
    email: document.getElementById('regEmail').value.trim(),
    password: document.getElementById('regPass').value,
    full_name: document.getElementById('regName').value.trim()
  };

  try {
    const res = await fetch(`${API_BASE}/api/auth/register`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const data = await res.json();

    if (res.ok && data.status === 'ok') {
      showToast('Registration successful! Please sign in.', 'success');
      document.getElementById('tabLogin').click();
      document.getElementById('loginUser').value = payload.username;
    } else {
      showToast(data.message || 'Registration failed', 'error');
    }
  } catch (err) {
    showToast('Network error on register', 'error');
  }
}

async function quickLogin(user, pass) {
  document.getElementById('loginUser').value = user;
  document.getElementById('loginPass').value = pass;
  document.getElementById('loginForm').dispatchEvent(new Event('submit'));
  document.getElementById('adminModal').classList.remove('open');
}

function setAuthSession(token, user) {
  state.token = token;
  state.user = user;
  localStorage.setItem('foodrush_token', token);
  localStorage.setItem('foodrush_user', JSON.stringify(user));
  updateAuthUI();
}

function logoutUser() {
  state.token = '';
  state.user = null;
  localStorage.removeItem('foodrush_token');
  localStorage.removeItem('foodrush_user');
  updateAuthUI();
  showToast('Logged out successfully', 'info');
}

function updateAuthUI() {
  const authBtn = document.getElementById('authBtn');
  if (state.token && state.user) {
    authBtn.textContent = `Sign Out (${state.user.username})`;
    authBtn.classList.remove('ghost-btn');
    authBtn.classList.add('btn-secondary');
  } else {
    authBtn.textContent = 'Sign In';
    authBtn.classList.add('ghost-btn');
    authBtn.classList.remove('btn-secondary');
  }
}

// ── Toast Notification Helper ─────────────────────────────────────────────────
function showToast(message, type = 'info') {
  const container = document.getElementById('toastContainer');
  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;
  toast.innerHTML = `<span>${type === 'success' ? '✓' : type === 'error' ? '✕' : 'ℹ'}</span> <span>${message}</span>`;
  container.appendChild(toast);

  setTimeout(() => {
    toast.style.opacity = '0';
    toast.style.transform = 'translateY(10px)';
    toast.style.transition = 'all 0.3s ease';
    setTimeout(() => toast.remove(), 300);
  }, 3500);
}
