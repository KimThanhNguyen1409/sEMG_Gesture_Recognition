function initQuizzes() {
  document.querySelectorAll('.quiz-card').forEach((card) => {
    const correctIdx = parseInt(card.getAttribute('data-correct'), 10);
    const explain = card.getAttribute('data-explain') || '';
    const feedback = card.querySelector('.quiz-feedback');
    const buttons = card.querySelectorAll('.quiz-option');

    buttons.forEach((btn, idx) => {
      btn.addEventListener('click', () => {
        buttons.forEach((b) => b.classList.remove('correct', 'wrong'));
        if (idx === correctIdx) {
          btn.classList.add('correct');
          feedback.textContent = 'Chính xác! ' + explain;
          feedback.style.color = '#15803d';
        } else {
          btn.classList.add('wrong');
          feedback.textContent = 'Chưa đúng, thử suy nghĩ lại xem bước nào giữ trạng thái ổn định nhé!';
          feedback.style.color = '#b91c1c';
        }
      });
    });
  });
}

document.addEventListener('DOMContentLoaded', initQuizzes);
